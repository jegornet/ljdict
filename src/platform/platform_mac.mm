#include "platform/platform.hpp"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>

#include <map>

@interface LjDictStatusTarget : NSObject
@property(nonatomic) platform::StatusItemActions actions;
@end

@implementation LjDictStatusTarget
- (void)clicked:(NSStatusBarButton*)sender {
    if (NSApp.currentEvent.type == NSEventTypeRightMouseUp) {
        NSMenu* menu = [[NSMenu alloc] init];
        [menu addItemWithTitle:@"Show LjDict" action:@selector(show) keyEquivalent:@""].target = self;
        [menu addItemWithTitle:@"Settings…" action:@selector(settings) keyEquivalent:@""].target = self;
        [menu addItem:NSMenuItem.separatorItem];
        [menu addItemWithTitle:@"Quit LjDict" action:@selector(quit) keyEquivalent:@""].target = self;
        [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, sender.bounds.size.height + 5) inView:sender];
    } else {
        self.actions.toggle();
    }
}
- (void)show { self.actions.show(); }
- (void)settings { self.actions.settings(); }
- (void)quit { self.actions.quit(); }
@end

@interface LjDictReopenTarget : NSObject
@property(nonatomic) std::function<void()> handler;
@end

@implementation LjDictReopenTarget
- (void)handleReopen:(NSAppleEventDescriptor*)event reply:(NSAppleEventDescriptor*)reply {
    self.handler();
}
@end

namespace platform {
namespace {

constexpr uint16_t escapeKeyCode = 53;

const std::map<int, const char*> functionKeys = {
    {kVK_F1, "F1"}, {kVK_F2, "F2"}, {kVK_F3, "F3"}, {kVK_F4, "F4"}, {kVK_F5, "F5"}, {kVK_F6, "F6"},
    {kVK_F7, "F7"}, {kVK_F8, "F8"}, {kVK_F9, "F9"}, {kVK_F10, "F10"}, {kVK_F11, "F11"}, {kVK_F12, "F12"},
    {kVK_F13, "F13"}, {kVK_F14, "F14"}, {kVK_F15, "F15"}, {kVK_F16, "F16"}, {kVK_F17, "F17"},
    {kVK_F18, "F18"}, {kVK_F19, "F19"}, {kVK_F20, "F20"},
};

const std::map<int, const char*> namedKeys = {
    {kVK_Space, "Space"}, {kVK_Return, "↩"}, {kVK_Tab, "⇥"}, {kVK_Delete, "⌫"}, {kVK_ForwardDelete, "⌦"},
    {kVK_LeftArrow, "←"}, {kVK_RightArrow, "→"}, {kVK_UpArrow, "↑"}, {kVK_DownArrow, "↓"},
    {kVK_Home, "↖"}, {kVK_End, "↘"}, {kVK_PageUp, "⇞"}, {kVK_PageDown, "⇟"},
};

/// The Latin letter on this key, so the shortcut reads the same under a Cyrillic layout.
std::optional<std::string> characterFor(uint16_t keyCode) {
    TISInputSourceRef source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
    if (!source) return std::nullopt;
    std::optional<std::string> result;
    auto layoutData = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    if (layoutData) {
        auto layout = reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(layoutData));
        UInt32 deadKeys = 0;
        UniChar characters[4];
        UniCharCount length = 0;
        OSStatus status = UCKeyTranslate(layout, keyCode, kUCKeyActionDisplay, 0, LMGetKbdType(),
                                         kUCKeyTranslateNoDeadKeysBit, &deadKeys, 4, &length, characters);
        if (status == noErr && length > 0) {
            NSString* text = [NSString stringWithCharacters:characters length:length];
            result = text.uppercaseString.UTF8String;
        }
    }
    CFRelease(source);
    return result;
}

/// Nil for keys that would swallow ordinary typing: only function keys may go without a modifier.
std::optional<HotKey> hotKeyFor(NSEvent* event) {
    NSEventModifierFlags flags = event.modifierFlags;
    uint32_t modifiers = 0;
    if (flags & NSEventModifierFlagControl) modifiers |= controlKey;
    if (flags & NSEventModifierFlagOption) modifiers |= optionKey;
    if (flags & NSEventModifierFlagShift) modifiers |= shiftKey;
    if (flags & NSEventModifierFlagCommand) modifiers |= cmdKey;
    bool hasCommandLikeModifier = modifiers & (controlKey | optionKey | cmdKey);
    if (!hasCommandLikeModifier && !functionKeys.contains(event.keyCode)) return std::nullopt;
    return HotKey{event.keyCode, modifiers};
}

EventHotKeyRef hotKeyRef = nullptr;
EventHandlerRef hotKeyHandlerRef = nullptr;
std::function<void()> hotKeyPressed;
id captureMonitor = nil;
NSStatusItem* statusItem = nil;
LjDictStatusTarget* statusTarget = nil;
LjDictReopenTarget* reopenTarget = nil;
bool hidesToMenuBar = false;
bool observingWindows = false;

/// The `character.book.closed` look with our own letters: no symbol has them, so they go on the plain book.
/// A template image, so the system tints it to match the menu bar.
NSImage* statusIcon() {
    NSImage* book = [[NSImage imageWithSystemSymbolName:@"book.closed" accessibilityDescription:nil]
        imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:15 weight:NSFontWeightRegular]];
    NSSize size = book.size;
    NSImage* image = [NSImage imageWithSize:size flipped:NO drawingHandler:^BOOL(NSRect rect) {
        [book drawInRect:rect];
        NSAttributedString* text = [[NSAttributedString alloc] initWithString:@"Lj" attributes:@{
            NSFontAttributeName: [NSFont systemFontOfSize:7.5 weight:NSFontWeightSemibold],
            NSForegroundColorAttributeName: NSColor.blackColor,
        }];
        NSSize textSize = text.size;
        // The spine takes up the left part of the cover.
        [text drawAtPoint:NSMakePoint(size.width * 0.57 - textSize.width / 2, (size.height - textSize.height) / 2 + 1.7)];
        return YES;
    }];
    [image setTemplate:YES];
    image.accessibilityDescription = @"LjDict";
    return image;
}

void observeWindows() {
    if (observingWindows) return;
    observingWindows = true;
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    for (NSNotificationName name in @[NSApplicationDidHideNotification, NSApplicationDidUnhideNotification]) {
        [center addObserverForName:name object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
            updateDockIcon();
        }];
    }
    [center addObserverForName:NSWindowWillCloseNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
        // The closing window still counts as visible during the notification.
        dispatch_async(dispatch_get_main_queue(), ^{ updateDockIcon(); });
    }];
}

}

HotKey defaultHotKey() {
    return {kVK_ANSI_D, cmdKey | shiftKey};
}

std::string displayName(const HotKey& key) {
    std::string name;
    if (key.modifiers & controlKey) name += "⌃";
    if (key.modifiers & optionKey) name += "⌥";
    if (key.modifiers & shiftKey) name += "⇧";
    if (key.modifiers & cmdKey) name += "⌘";
    int code = static_cast<int>(key.keyCode);
    if (auto found = functionKeys.find(code); found != functionKeys.end()) return name + found->second;
    if (auto found = namedKeys.find(code); found != namedKeys.end()) return name + found->second;
    if (auto character = characterFor(static_cast<uint16_t>(code))) return name + *character;
    return name + "Key " + std::to_string(key.keyCode);
}

/// Carbon's hot key API needs no Accessibility permission, and key codes are physical,
/// so the shortcut works under a Cyrillic layout.
bool registerHotKey(const HotKey& key, std::function<void()> onPress) {
    unregisterHotKey();
    hotKeyPressed = std::move(onPress);
    if (!hotKeyHandlerRef) {
        EventTypeSpec eventType{kEventClassKeyboard, kEventHotKeyPressed};
        InstallEventHandler(GetApplicationEventTarget(), [](EventHandlerCallRef, EventRef, void*) -> OSStatus {
            // The handler may hide a window, which GTK must not see from inside this callback.
            dispatch_async(dispatch_get_main_queue(), ^{ if (hotKeyPressed) hotKeyPressed(); });
            return noErr;
        }, 1, &eventType, nullptr, &hotKeyHandlerRef);
    }
    EventHotKeyID identifier{0x4C4A4443, 1}; // "LJDC"
    return RegisterEventHotKey(key.keyCode, key.modifiers, identifier, GetApplicationEventTarget(), 0, &hotKeyRef) == noErr;
}

void unregisterHotKey() {
    if (hotKeyRef) UnregisterEventHotKey(hotKeyRef);
    hotKeyRef = nullptr;
}

// A local monitor sees Command combinations too, which menus would otherwise take first.
void captureHotKey(std::function<void(std::optional<HotKey>)> done) {
    cancelHotKeyCapture();
    auto callback = std::make_shared<std::function<void(std::optional<HotKey>)>>(std::move(done));
    captureMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent*(NSEvent* event) {
        if (event.keyCode == escapeKeyCode) {
            cancelHotKeyCapture();
            (*callback)(std::nullopt);
        } else if (auto key = hotKeyFor(event)) {
            cancelHotKeyCapture();
            (*callback)(key);
        } else {
            NSBeep();
        }
        return nil;
    }];
}

void cancelHotKeyCapture() {
    if (captureMonitor) [NSEvent removeMonitor:captureMonitor];
    captureMonitor = nil;
}

bool systemOwnsHotKey() { return false; }
bool hasMenuBarIcon() { return true; }
void configureHotKey() {}
void setHotKeyStatusHandler(std::function<void()>) {}
std::optional<std::string> hotKeyNotice() { return std::nullopt; }

void hideApp() {
    [NSApp hide:nil];
}

void activateApp() {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp unhide:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

bool appIsActive() {
    return NSApp.isActive;
}

bool systemIsDark() {
    return [NSApp.effectiveAppearance bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]]
        == NSAppearanceNameDarkAqua;
}

void setHideToMenuBar(bool enabled, const StatusItemActions& actions) {
    observeWindows();
    hidesToMenuBar = enabled;
    if (enabled && !statusItem) {
        statusTarget = [[LjDictStatusTarget alloc] init];
        statusTarget.actions = actions;
        statusItem = [NSStatusBar.systemStatusBar statusItemWithLength:NSSquareStatusItemLength];
        statusItem.button.image = statusIcon();
        statusItem.button.target = statusTarget;
        statusItem.button.action = @selector(clicked:);
        [statusItem.button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
    } else if (!enabled && statusItem) {
        [NSStatusBar.systemStatusBar removeStatusItem:statusItem];
        statusItem = nil;
    }
    updateDockIcon();
}

/// A minimized window lives in the Dock, so it keeps the icon there.
void updateDockIcon() {
    bool onScreen = false;
    if (!NSApp.isHidden) {
        for (NSWindow* window in NSApp.windows) {
            if (window.isVisible || window.isMiniaturized) onScreen = true;
        }
    }
    [NSApp setActivationPolicy:hidesToMenuBar && !onScreen ? NSApplicationActivationPolicyAccessory
                                                           : NSApplicationActivationPolicyRegular];
}

void setReopenHandler(std::function<void()> handler) {
    reopenTarget = [[LjDictReopenTarget alloc] init];
    reopenTarget.handler = std::move(handler);
    [NSAppleEventManager.sharedAppleEventManager setEventHandler:reopenTarget
                                                     andSelector:@selector(handleReopen:reply:)
                                                   forEventClass:kCoreEventClass
                                                      andEventID:kAEReopenApplication];
}

std::vector<unsigned char> appIconPNG() {
    constexpr int side = 256;
    NSImage* icon = NSApp.applicationIconImage;
    if (!icon) return {};
    NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:side pixelsHigh:side
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
    [icon drawInRect:NSMakeRect(0, 0, side, side)];
    [NSGraphicsContext restoreGraphicsState];
    NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    auto bytes = static_cast<const unsigned char*>(png.bytes);
    return {bytes, bytes + png.length};
}

bool loginItemEnabled() {
    return SMAppService.mainAppService.status == SMAppServiceStatusEnabled;
}

std::optional<std::string> setLoginItemEnabled(bool enabled) {
    NSError* error = nil;
    SMAppService* service = SMAppService.mainAppService;
    BOOL succeeded = enabled ? [service registerAndReturnError:&error] : [service unregisterAndReturnError:&error];
    if (!succeeded) return std::string(error.localizedDescription.UTF8String);
    return loginItemNotice();
}

std::optional<std::string> loginItemNotice() {
    if (SMAppService.mainAppService.status == SMAppServiceStatusRequiresApproval) {
        return "Allow LjDict in System Settings → General → Login Items.";
    }
    return std::nullopt;
}

std::optional<LegacySettings> readLegacySettings() {
    NSUserDefaults* defaults = [[NSUserDefaults alloc] initWithSuiteName:@"net.jegor.ljdict"];
    if (!defaults || [defaults objectForKey:@"dictionaryPath"] == nil && [defaults objectForKey:@"history"] == nil
        && [defaults objectForKey:@"dictionaryOrder"] == nil && [defaults objectForKey:@"disabledDictionaries"] == nil) {
        return std::nullopt;
    }
    auto strings = [&](NSString* key) {
        std::vector<std::string> result;
        for (NSString* value in [defaults stringArrayForKey:key]) result.emplace_back(value.UTF8String);
        return result;
    };
    LegacySettings legacy;
    if (NSString* path = [defaults stringForKey:@"dictionaryPath"]) legacy.dictionaryPath = path.UTF8String;
    legacy.disabled = strings(@"disabledDictionaries");
    legacy.order = strings(@"dictionaryOrder");
    legacy.history = strings(@"history");
    legacy.startMinimized = [defaults boolForKey:@"startMinimized"];
    legacy.hideToMenuBar = [defaults boolForKey:@"hideToMenuBar"];
    if (NSData* data = [defaults dataForKey:@"hotKey"]) {
        NSDictionary* json = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        if ([json isKindOfClass:NSDictionary.class] && json[@"keyCode"] && json[@"modifiers"]) {
            legacy.hotKey = HotKey{[json[@"keyCode"] unsignedIntValue], [json[@"modifiers"] unsignedIntValue]};
        }
    }
    return legacy;
}

}
