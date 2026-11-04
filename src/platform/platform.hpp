#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/// What GTK does not offer: everything here talks to the system directly.
namespace platform {

struct HotKey {
    uint32_t keyCode = 0;
    uint32_t modifiers = 0;
    bool operator==(const HotKey&) const = default;
};

HotKey defaultHotKey();
std::string displayName(const HotKey& key);

/// False when another application already owns the shortcut.
bool registerHotKey(const HotKey& key, std::function<void()> onPress);
void unregisterHotKey();

/// Captures the next key press. Calls `done` with the shortcut, or with nothing on Esc.
void captureHotKey(std::function<void(std::optional<HotKey>)> done);
void cancelHotKeyCapture();

void hideApp();
void activateApp();
bool appIsActive();
bool systemIsDark();

struct StatusItemActions {
    std::function<void()> toggle;
    std::function<void()> show;
    std::function<void()> settings;
    std::function<void()> quit;
};

/// Keeps a menu bar icon while `enabled`, and leaves the Dock while no window is on screen.
void setHideToMenuBar(bool enabled, const StatusItemActions& actions);
void updateDockIcon();

/// Called when the app is reopened with nothing on screen, such as by a click on its Dock icon.
void setReopenHandler(std::function<void()> handler);

/// The app icon as PNG data, empty if there is none.
std::vector<unsigned char> appIconPNG();

bool loginItemEnabled();
/// A message for the user, if there is something they need to know.
std::optional<std::string> setLoginItemEnabled(bool enabled);
std::optional<std::string> loginItemNotice();

/// Settings of the AppKit version, read once so that moving to this one keeps them.
struct LegacySettings {
    std::optional<std::string> dictionaryPath;
    std::vector<std::string> disabled;
    std::vector<std::string> order;
    std::vector<std::string> history;
    std::optional<HotKey> hotKey;
    bool startMinimized = false;
    bool hideToMenuBar = false;
};
std::optional<LegacySettings> readLegacySettings();

}
