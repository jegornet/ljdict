#include "platform/platform.hpp"

#include <gtk/gtk.h>

// Elsewhere the window manager owns global shortcuts, so most of these do nothing.
namespace platform {

HotKey defaultHotKey() { return {}; }
std::string displayName(const HotKey&) { return "Not available"; }
bool registerHotKey(const HotKey&, std::function<void()>) { return true; }
void unregisterHotKey() {}
void captureHotKey(std::function<void(std::optional<HotKey>)> done) { done(std::nullopt); }
void cancelHotKeyCapture() {}
void hideApp() {
    GList* windows = gtk_window_list_toplevels();
    for (GList* item = windows; item; item = item->next) gtk_widget_set_visible(GTK_WIDGET(item->data), false);
    g_list_free(windows);
}
void activateApp() {}
bool appIsActive() { return true; }
bool systemIsDark() { return false; }
void setHideToMenuBar(bool, const StatusItemActions&) {}
void updateDockIcon() {}
void setReopenHandler(std::function<void()>) {}
std::vector<unsigned char> appIconPNG() { return {}; }
bool loginItemEnabled() { return false; }
std::optional<std::string> setLoginItemEnabled(bool) { return "Not available on this system."; }
std::optional<std::string> loginItemNotice() { return std::nullopt; }
std::optional<LegacySettings> readLegacySettings() { return std::nullopt; }

}
