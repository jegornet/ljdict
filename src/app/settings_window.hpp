#pragma once

#include "app/app_model.hpp"

#include <gtkmm.h>

#include <functional>

/// Deliberately a column of frames with no outer scrolling: everything stays on screen and the
/// dictionary list takes the spare height.
class SettingsWindow : public Gtk::Window {
public:
    SettingsWindow(AppModel& model, std::function<void()> hotKeyChanged, std::function<void()> hideToMenuBarChanged);

    void refresh();
    void setHotKeyTaken(bool taken) { hotKeyTaken_.set_visible(taken); }

private:
    Gtk::Widget* buildDictionaries();
    Gtk::Widget* buildShortcut();
    Gtk::Widget* buildStartup();
    Gtk::Widget* makeSwitchRow(const Glib::ustring& title, Gtk::Switch& control);
    Gtk::Widget* makeBookRow(size_t index, const std::shared_ptr<const ljdict::StarDictBook>& book);
    void applyPath();
    void chooseFolder();
    void refreshBooks();
    void refreshHotKey();
    void startRecording();
    void stopRecording();
    void setHotKey(const platform::HotKey& key);
    void setLaunchAtLogin(bool enabled);

    AppModel& model_;
    std::function<void()> hotKeyChanged_;
    std::function<void()> hideToMenuBarChanged_;

    Gtk::Entry pathEntry_;
    Gtk::ListBox bookList_;
    Gtk::Label emptyLabel_{"No StarDict dictionaries found in this folder."};
    Gtk::Spinner spinner_;
    Gtk::Box failureBox_{Gtk::Orientation::VERTICAL, 2};
    Gtk::Button recorder_;
    Gtk::Button resetHotKey_{"Reset"};
    Gtk::Label hotKeyTaken_{"Another application is using this shortcut."};
    Gtk::Switch hideToMenuBar_;
    Gtk::Switch launchAtLogin_;
    Gtk::Switch startMinimized_;
    Gtk::Label loginItemMessage_;
    bool isRecording_ = false;
    bool updatingLoginItem_ = false;
};
