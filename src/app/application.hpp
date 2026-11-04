#pragma once

#include "app/app_model.hpp"
#include "app/main_window.hpp"
#include "app/settings_window.hpp"

#include <gtkmm.h>

#include <memory>

class Application : public Gtk::Application {
public:
    static Glib::RefPtr<Application> create();

protected:
    Application();
    void on_startup() override;
    void on_activate() override;
    void on_shutdown() override;

private:
    void installMenu();
    void installStyle();
    void registerHotKey();
    void updateStatusItem();
    void toggleWindow();
    void showWindow();
    void present(Gtk::Window& window);
    void showSettings();
    void showAbout();

    AppModel model_;
    MainWindow* mainWindow_ = nullptr;
    std::unique_ptr<SettingsWindow> settingsWindow_;
    Gtk::AboutDialog* aboutDialog_ = nullptr;
    bool activated_ = false;
};
