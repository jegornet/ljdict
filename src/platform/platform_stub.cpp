#include "platform/platform.hpp"

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <cerrno>
#include <memory>

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

namespace {

// An XDG autostart entry, read by GNOME, KDE and most other desktops.
std::string autostartPath() {
    return std::string(g_get_user_config_dir()) + "/autostart/net.jegor.ljdict.gtk.desktop";
}

// The running binary, so that a build outside the PATH starts too. Quoted as the Exec key requires.
std::string execLine() {
    std::unique_ptr<char, decltype(&g_free)> target(g_file_read_link("/proc/self/exe", nullptr), g_free);
    if (!target) return "ljdict";
    std::string line = "\"";
    for (const char* c = target.get(); *c; ++c) {
        if (*c == '"' || *c == '`' || *c == '$' || *c == '\\') line += '\\';
        line += *c;
    }
    return line + "\"";
}

}

bool loginItemEnabled() {
    std::unique_ptr<GKeyFile, decltype(&g_key_file_free)> file(g_key_file_new(), g_key_file_free);
    if (!g_key_file_load_from_file(file.get(), autostartPath().c_str(), G_KEY_FILE_NONE, nullptr)) return false;
    const char* group = G_KEY_FILE_DESKTOP_GROUP;
    if (g_key_file_get_boolean(file.get(), group, G_KEY_FILE_DESKTOP_KEY_HIDDEN, nullptr)) return false;
    GError* error = nullptr;
    bool enabled = g_key_file_get_boolean(file.get(), group, "X-GNOME-Autostart-enabled", &error);
    if (error) {
        g_error_free(error);
        return true;
    }
    return enabled;
}

std::optional<std::string> setLoginItemEnabled(bool enabled) {
    std::string path = autostartPath();
    if (!enabled) {
        if (g_remove(path.c_str()) != 0 && errno != ENOENT) return std::string("Could not remove ") + path + ".";
        return std::nullopt;
    }
    std::unique_ptr<char, decltype(&g_free)> folder(g_path_get_dirname(path.c_str()), g_free);
    if (g_mkdir_with_parents(folder.get(), 0700) != 0) return std::string("Could not create ") + folder.get() + ".";

    std::unique_ptr<GKeyFile, decltype(&g_key_file_free)> file(g_key_file_new(), g_key_file_free);
    const char* group = G_KEY_FILE_DESKTOP_GROUP;
    g_key_file_set_string(file.get(), group, G_KEY_FILE_DESKTOP_KEY_TYPE, G_KEY_FILE_DESKTOP_TYPE_APPLICATION);
    g_key_file_set_string(file.get(), group, G_KEY_FILE_DESKTOP_KEY_NAME, "LjDict");
    g_key_file_set_string(file.get(), group, G_KEY_FILE_DESKTOP_KEY_EXEC, execLine().c_str());
    g_key_file_set_string(file.get(), group, G_KEY_FILE_DESKTOP_KEY_ICON, "net.jegor.ljdict.gtk");
    g_key_file_set_boolean(file.get(), group, G_KEY_FILE_DESKTOP_KEY_TERMINAL, false);
    g_key_file_set_boolean(file.get(), group, "X-GNOME-Autostart-enabled", true);
    GError* error = nullptr;
    if (!g_key_file_save_to_file(file.get(), path.c_str(), &error)) {
        std::string message = error->message;
        g_error_free(error);
        return message;
    }
    return std::nullopt;
}
std::optional<std::string> loginItemNotice() { return std::nullopt; }
std::optional<LegacySettings> readLegacySettings() { return std::nullopt; }

}
