#include "platform/platform.hpp"

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>

// Elsewhere the desktop owns global shortcuts, so most of these do nothing. The shortcut goes
// through the GlobalShortcuts portal: the app asks for one and the desktop lets the user pick keys.
namespace platform {

namespace {

constexpr const char* portalName = "org.freedesktop.portal.Desktop";
constexpr const char* portalPath = "/org/freedesktop/portal/desktop";
constexpr const char* shortcutsInterface = "org.freedesktop.portal.GlobalShortcuts";
constexpr const char* shortcutId = "toggle-window";

struct Portal {
    enum class Status { idle, waiting, bound, failed };

    GDBusConnection* bus = nullptr;
    std::string session;
    std::string trigger;
    Status status = Status::idle;
    unsigned requests = 0;
    std::function<void()> onPress;
    std::function<void()> onStatus;
    bool cannotConfigure = false;
};

Portal portal;

void notifyStatus() {
    if (portal.onStatus) portal.onStatus();
}

void fail(const std::string& reason) {
    std::cerr << "Global shortcut: " << reason << '\n';
    portal.status = Portal::Status::failed;
    notifyStatus();
}

GVariant* emptyOptions() {
    return g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0);
}

using ResponseHandler = std::function<void(guint32 response, GVariant* results, const char* error)>;

struct PendingRequest {
    ResponseHandler done;
    guint subscription = 0;
};

/// Portal methods answer through a Request object whose path follows from the token, so the
/// Response signal is subscribed before the call and cannot be missed.
void request(const char* method, const std::function<GVariant*(const char* token)>& parameters, ResponseHandler done) {
    std::string token = "ljdict" + std::to_string(++portal.requests);
    std::string sender = g_dbus_connection_get_unique_name(portal.bus) + 1;
    for (char& c : sender) if (c == '.') c = '_';
    std::string path = std::string(portalPath) + "/request/" + sender + "/" + token;

    auto* pending = new PendingRequest{std::move(done)};
    pending->subscription = g_dbus_connection_signal_subscribe(portal.bus, portalName, "org.freedesktop.portal.Request",
        "Response", path.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection* bus, const char*, const char*, const char*, const char*, GVariant* parameters, gpointer data) {
            auto* pending = static_cast<PendingRequest*>(data);
            g_dbus_connection_signal_unsubscribe(bus, pending->subscription);
            guint32 response = 2;
            GVariant* results = nullptr;
            g_variant_get(parameters, "(u@a{sv})", &response, &results);
            auto done = std::move(pending->done);
            delete pending;
            done(response, results, nullptr);
            g_variant_unref(results);
        },
        pending, nullptr);

    g_dbus_connection_call(portal.bus, portalName, portalPath, shortcutsInterface, method, parameters(token.c_str()),
        G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
        [](GObject* source, GAsyncResult* result, gpointer data) {
            GError* error = nullptr;
            GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
            if (reply) {
                g_variant_unref(reply);
                return;
            }
            auto* pending = static_cast<PendingRequest*>(data);
            g_dbus_connection_signal_unsubscribe(G_DBUS_CONNECTION(source), pending->subscription);
            auto done = std::move(pending->done);
            delete pending;
            done(2, nullptr, error->message);
            g_error_free(error);
        },
        pending);
}

/// GNOME describes the keys as a sentence around an accelerator, such as "Press <Control><Alt>d".
std::string readableTrigger(const char* description) {
    if (const char* accelerator = strchr(description, '<')) {
        guint key;
        GdkModifierType modifiers;
        if (gtk_accelerator_parse(accelerator, &key, &modifiers)) {
            std::unique_ptr<char, decltype(&g_free)> label(gtk_accelerator_get_label(key, modifiers), g_free);
            return label.get();
        }
    }
    return description;
}

void readShortcuts(GVariant* shortcuts) {
    GVariantIter iter;
    g_variant_iter_init(&iter, shortcuts);
    const char* id;
    GVariant* properties;
    while (g_variant_iter_loop(&iter, "(&s@a{sv})", &id, &properties)) {
        if (std::string_view(id) != shortcutId) continue;
        const char* trigger = "";
        g_variant_lookup(properties, "trigger_description", "&s", &trigger);
        portal.trigger = readableTrigger(trigger);
    }
}

void bindShortcut() {
    portal.status = Portal::Status::waiting;
    notifyStatus();
    request("BindShortcuts",
        [](const char* token) {
            GVariantBuilder properties;
            g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&properties, "{sv}", "description", g_variant_new_string("Show or hide LjDict"));
            g_variant_builder_add(&properties, "{sv}", "preferred_trigger", g_variant_new_string("SHIFT+ALT+d"));
            GVariantBuilder shortcuts;
            g_variant_builder_init(&shortcuts, G_VARIANT_TYPE("a(sa{sv})"));
            g_variant_builder_add(&shortcuts, "(s@a{sv})", shortcutId, g_variant_builder_end(&properties));
            GVariantBuilder options;
            g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token));
            return g_variant_new("(o@a(sa{sv})s@a{sv})", portal.session.c_str(), g_variant_builder_end(&shortcuts), "",
                g_variant_builder_end(&options));
        },
        [](guint32 response, GVariant* results, const char* error) {
            if (error) return fail(error);
            // A dismissed dialog leaves the shortcut without keys; the user can open it again.
            portal.trigger.clear();
            if (response == 0) {
                if (GVariant* shortcuts = g_variant_lookup_value(results, "shortcuts", G_VARIANT_TYPE("a(sa{sv})"))) {
                    readShortcuts(shortcuts);
                    g_variant_unref(shortcuts);
                }
            }
            portal.status = Portal::Status::bound;
            notifyStatus();
        });
}

void createSession() {
    request("CreateSession",
        [](const char* token) {
            GVariantBuilder options;
            g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token));
            g_variant_builder_add(&options, "{sv}", "session_handle_token", g_variant_new_string("ljdict"));
            return g_variant_new("(@a{sv})", g_variant_builder_end(&options));
        },
        [](guint32 response, GVariant* results, const char* error) {
            if (error) return fail(error);
            GVariant* handle = response == 0 ? g_variant_lookup_value(results, "session_handle", nullptr) : nullptr;
            if (!handle) return fail("the desktop refused to create a session");
            portal.session = g_variant_get_string(handle, nullptr);
            g_variant_unref(handle);
            bindShortcut();
        });
}

/// Wayland raises a window only for an activation token, and the key press earned one.
void onActivated(GDBusConnection*, const char*, const char*, const char*, const char*, GVariant* parameters, gpointer) {
    const char* session;
    const char* id;
    guint64 timestamp;
    GVariant* options;
    g_variant_get(parameters, "(&o&st@a{sv})", &session, &id, &timestamp, &options);
    if (session == portal.session && std::string_view(id) == shortcutId && portal.onPress) {
        const char* token = nullptr;
        if (g_variant_lookup(options, "activation_token", "&s", &token)) {
            GList* windows = gtk_window_list_toplevels();
            for (GList* item = windows; item; item = item->next) {
                if (GTK_IS_APPLICATION_WINDOW(item->data)) gtk_window_set_startup_id(GTK_WINDOW(item->data), token);
            }
            g_list_free(windows);
        }
        portal.onPress();
    }
    g_variant_unref(options);
}

void onShortcutsChanged(GDBusConnection*, const char*, const char*, const char*, const char*, GVariant* parameters, gpointer) {
    const char* session;
    GVariant* shortcuts;
    g_variant_get(parameters, "(&o@a(sa{sv}))", &session, &shortcuts);
    if (session == portal.session) {
        portal.trigger.clear();
        readShortcuts(shortcuts);
        notifyStatus();
    }
    g_variant_unref(shortcuts);
}

/// A connection of its own, because a host app must name itself to the portal before any other
/// call on the connection, and GTK has already used the shared one.
void connectPortal() {
    portal.status = Portal::Status::waiting;
    notifyStatus();
    if (!portal.bus) {
        GError* error = nullptr;
        std::unique_ptr<char, decltype(&g_free)> address(g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &error), g_free);
        if (address) {
            portal.bus = g_dbus_connection_new_for_address_sync(address.get(),
                GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
                nullptr, nullptr, &error);
        }
        if (!portal.bus) {
            fail(error->message);
            g_error_free(error);
            return;
        }
        g_dbus_connection_signal_subscribe(portal.bus, portalName, shortcutsInterface, "Activated", portalPath, nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE, onActivated, nullptr, nullptr);
        g_dbus_connection_signal_subscribe(portal.bus, portalName, shortcutsInterface, "ShortcutsChanged", portalPath, nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE, onShortcutsChanged, nullptr, nullptr);
    }
    // Older portals have no registry; then the desktop works out the app from its process.
    GApplication* application = g_application_get_default();
    const char* id = application ? g_application_get_application_id(application) : nullptr;
    g_dbus_connection_call(portal.bus, portalName, portalPath, "org.freedesktop.host.portal.Registry", "Register",
        g_variant_new("(s@a{sv})", id ? id : "", emptyOptions()), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
        [](GObject* source, GAsyncResult* result, gpointer) {
            if (GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, nullptr)) g_variant_unref(reply);
            createSession();
        },
        nullptr);
}

}

HotKey defaultHotKey() { return {}; }

std::string displayName(const HotKey&) {
    switch (portal.status) {
        case Portal::Status::idle:
        case Portal::Status::waiting: return "…";
        case Portal::Status::failed: return "Not available";
        case Portal::Status::bound: return portal.trigger.empty() ? "Choose…" : portal.trigger;
    }
    return {};
}

bool registerHotKey(const HotKey&, std::function<void()> onPress) {
    portal.onPress = std::move(onPress);
    if (portal.status == Portal::Status::idle) connectPortal();
    return true;
}

void unregisterHotKey() { portal.onPress = nullptr; }
void captureHotKey(std::function<void(std::optional<HotKey>)> done) { done(std::nullopt); }
void cancelHotKeyCapture() {}

bool systemOwnsHotKey() { return true; }

void configureHotKey() {
    switch (portal.status) {
        case Portal::Status::waiting: return;
        case Portal::Status::idle:
        case Portal::Status::failed: return connectPortal();
        case Portal::Status::bound: break;
    }
    if (portal.trigger.empty()) return bindShortcut();
    // Only portal version 2 can reopen the dialog for a shortcut that already has keys, and GNOME 50
    // does not implement it.
    g_dbus_connection_call(portal.bus, portalName, portalPath, shortcutsInterface, "ConfigureShortcuts",
        g_variant_new("(os@a{sv})", portal.session.c_str(), "", emptyOptions()), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
        [](GObject* source, GAsyncResult* result, gpointer) {
            GError* error = nullptr;
            GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
            if (reply) {
                g_variant_unref(reply);
                return;
            }
            std::cerr << "Global shortcut: " << error->message << '\n';
            g_error_free(error);
            portal.cannotConfigure = true;
            notifyStatus();
        },
        nullptr);
}

void setHotKeyStatusHandler(std::function<void()> handler) { portal.onStatus = std::move(handler); }

std::optional<std::string> hotKeyNotice() {
    if (portal.status == Portal::Status::failed) return "This desktop does not offer global shortcuts to apps.";
    if (portal.cannotConfigure) return "This desktop changes the keys in its own settings, on the page for LjDict.";
    return std::nullopt;
}

void hideApp() {
    GList* windows = gtk_window_list_toplevels();
    for (GList* item = windows; item; item = item->next) gtk_widget_set_visible(GTK_WIDGET(item->data), false);
    g_list_free(windows);
}
void activateApp() {}
bool appIsActive() { return true; }
bool systemIsDark() { return false; }
bool hasMenuBarIcon() { return false; }
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
