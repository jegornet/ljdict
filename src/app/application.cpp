#include "app/application.hpp"

#include "platform/platform.hpp"

namespace {

/// GTK's <Primary> means Control on every system, so macOS needs Command spelled out.
#ifdef __APPLE__
#define COMMAND_KEY "<Meta>"
#else
#define COMMAND_KEY "<Primary>"
#endif

/// On macOS a second launch goes to the running app through the reopen handler. Elsewhere
/// uniqueness does that: a launch while the window is hidden shows it.
#ifdef __APPLE__
constexpr auto applicationFlags = Gio::Application::Flags::NON_UNIQUE;
#else
constexpr auto applicationFlags = Gio::Application::Flags::DEFAULT_FLAGS;
#endif

constexpr const char* styleSheet = R"css(
.content { background-color: #fefce9; color: #1c1c1c; }
.dark .content { background-color: #242320; color: #e6e4da; }
textview.article text { background-color: #fefce9; color: #1c1c1c; }
.dark textview.article text { background-color: #242320; color: #e6e4da; }
textview.article { font-size: 15px; }
.word-list row { padding: 0; min-height: 20px; }
.word-list row:selected { background-color: #d9d6c3; color: #1c1c1c; }
.dark .word-list row:selected { background-color: #4a4843; color: #e6e4da; }
.word { font-size: 14px; padding: 0 4px; min-height: 20px; }
.caption { font-size: 11px; }
)css";

}

Glib::RefPtr<Application> Application::create() {
    return Glib::make_refptr_for_instance<Application>(new Application());
}

Application::Application() : Gtk::Application("net.jegor.ljdict.gtk", applicationFlags) {
    Glib::set_application_name("LjDict");
}

void Application::on_startup() {
    Gtk::Application::on_startup();
    installStyle();
    installMenu();
    Gtk::Window::set_default_icon_name(get_id());
    hold(); // the app keeps running with no window, so the global shortcut stays alive
    platform::setReopenHandler([this] { showWindow(); });

    mainWindow_ = Gtk::make_managed<MainWindow>(model_);
    add_window(*mainWindow_);
}

void Application::on_activate() {
    if (activated_) {
        showWindow();
        return;
    }
    activated_ = true;
    registerHotKey();
    updateStatusItem();
    if (!model_.settings().startMinimized) showWindow();
    model_.reload();
}

void Application::on_shutdown() {
    if (mainWindow_) mainWindow_->saveGeometry();
    Gtk::Application::on_shutdown();
}

void Application::installStyle() {
    auto provider = Gtk::CssProvider::create();
    provider->load_from_string(styleSheet);
    Gtk::StyleProvider::add_provider_for_display(Gdk::Display::get_default(), provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void Application::installMenu() {
    add_action("about", [this] { showAbout(); });
    add_action("settings", [this] { showSettings(); });
    add_action("hide", [] { platform::hideApp(); });
    add_action("quit", [this] { quit(); });
    add_action("search", [this] { showWindow(); });
    add_action("close-window", [this] {
        if (auto window = get_active_window()) window->close();
    });
    add_action("minimize", [this] {
        if (auto window = get_active_window()) window->minimize();
    });
    add_action("zoom", [this] {
        auto window = get_active_window();
        if (!window) return;
        if (window->is_maximized()) window->unmaximize(); else window->maximize();
    });

    set_accel_for_action("app.settings", COMMAND_KEY "comma");
    set_accel_for_action("app.hide", COMMAND_KEY "h");
    set_accel_for_action("app.quit", COMMAND_KEY "q");
    set_accel_for_action("app.search", COMMAND_KEY "l");
    set_accel_for_action("app.close-window", COMMAND_KEY "w");
    set_accel_for_action("app.minimize", COMMAND_KEY "m");

    auto bar = Gio::Menu::create();

    auto app = Gio::Menu::create();
    auto appAbout = Gio::Menu::create();
    appAbout->append("About LjDict", "app.about");
    app->append_section(appAbout);
    auto appSettings = Gio::Menu::create();
    appSettings->append("Settings…", "app.settings");
    app->append_section(appSettings);
    auto appHide = Gio::Menu::create();
    appHide->append("Hide LjDict", "app.hide");
    app->append_section(appHide);
    auto appQuit = Gio::Menu::create();
    appQuit->append("Quit LjDict", "app.quit");
    app->append_section(appQuit);
    bar->append_submenu("LjDict", app);

    auto edit = Gio::Menu::create();
    edit->append("Search", "app.search");
    bar->append_submenu("Edit", edit);

    auto window = Gio::Menu::create();
    window->append("Close", "app.close-window");
    window->append("Minimize", "app.minimize");
    window->append("Zoom", "app.zoom");
    bar->append_submenu("Window", window);

    set_menubar(bar);
}

void Application::registerHotKey() {
    bool registered = platform::registerHotKey(model_.settings().hotKey, [this] { toggleWindow(); });
    if (settingsWindow_) settingsWindow_->setHotKeyTaken(!registered);
}

void Application::updateStatusItem() {
    platform::setHideToMenuBar(model_.settings().hideToMenuBar, {
        .toggle = [this] { toggleWindow(); },
        .show = [this] { showWindow(); },
        .settings = [this] { showSettings(); },
        .quit = [this] { quit(); },
    });
}

void Application::toggleWindow() {
    if (platform::appIsActive() && mainWindow_->is_active()) {
        platform::hideApp();
    } else {
        showWindow();
    }
}

void Application::showWindow() {
    platform::activateApp();
    mainWindow_->present();
    mainWindow_->focusSearch();
}

void Application::showSettings() {
    if (!settingsWindow_) {
        settingsWindow_ = std::make_unique<SettingsWindow>(model_, [this] { registerHotKey(); }, [this] { updateStatusItem(); });
        add_window(*settingsWindow_);
    }
    settingsWindow_->refresh();
    platform::activateApp();
    settingsWindow_->present();
}

void Application::showAbout() {
    if (!aboutDialog_) {
        aboutDialog_ = Gtk::make_managed<Gtk::AboutDialog>();
        aboutDialog_->set_program_name("LjDict");
        aboutDialog_->set_version(LJDICT_VERSION);
        aboutDialog_->set_comments("Lightweight dictionary");
        auto icon = platform::appIconPNG();
        if (!icon.empty()) aboutDialog_->set_logo(Gdk::Texture::create_from_bytes(Glib::Bytes::create(icon.data(), icon.size())));
        else aboutDialog_->set_logo_icon_name(get_id());
        aboutDialog_->set_hide_on_close(true);
    }
    aboutDialog_->present();
}
