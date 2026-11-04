#include "app/settings.hpp"

#include <glibmm.h>

#include <filesystem>
#include <iostream>

namespace {

constexpr const char* group = "LjDict";

std::string settingsPath() {
    return Glib::build_filename(Glib::get_user_config_dir(), "ljdict", "settings.ini");
}

std::vector<std::string> strings(const Glib::RefPtr<Glib::KeyFile>& file, const char* key) {
    std::vector<std::string> result;
    if (file->has_key(group, key)) {
        for (const Glib::ustring& value : file->get_string_list(group, key)) result.push_back(value);
    }
    return result;
}

Settings fromLegacy(const platform::LegacySettings& legacy) {
    Settings settings;
    if (legacy.dictionaryPath) settings.dictionaryPath = *legacy.dictionaryPath;
    settings.disabled = {legacy.disabled.begin(), legacy.disabled.end()};
    settings.order = legacy.order;
    settings.history = legacy.history;
    if (legacy.hotKey) settings.hotKey = *legacy.hotKey;
    settings.startMinimized = legacy.startMinimized;
    settings.hideToMenuBar = legacy.hideToMenuBar;
    return settings;
}

}

Settings Settings::load() {
    Settings settings;
    if (!Glib::file_test(settingsPath(), Glib::FileTest::EXISTS)) {
        auto legacy = platform::readLegacySettings();
        if (legacy) settings = fromLegacy(*legacy);
        return settings;
    }
    auto file = Glib::KeyFile::create();
    try {
        file->load_from_file(settingsPath());
        auto integer = [&](const char* key, int fallback) { return file->has_key(group, key) ? file->get_integer(group, key) : fallback; };
        auto boolean = [&](const char* key) { return file->has_key(group, key) && file->get_boolean(group, key); };
        if (file->has_key(group, "dictionaryPath")) settings.dictionaryPath = file->get_string(group, "dictionaryPath");
        auto disabled = strings(file, "disabledDictionaries");
        settings.disabled = {disabled.begin(), disabled.end()};
        settings.order = strings(file, "dictionaryOrder");
        settings.history = strings(file, "history");
        settings.startMinimized = boolean("startMinimized");
        settings.hideToMenuBar = boolean("hideToMenuBar");
        settings.hotKey.keyCode = integer("hotKeyCode", settings.hotKey.keyCode);
        settings.hotKey.modifiers = integer("hotKeyModifiers", settings.hotKey.modifiers);
        settings.windowWidth = integer("windowWidth", settings.windowWidth);
        settings.windowHeight = integer("windowHeight", settings.windowHeight);
        settings.leftPane = integer("leftPane", settings.leftPane);
        settings.articlePane = integer("articlePane", settings.articlePane);
    } catch (const Glib::Error& error) {
        std::cerr << "Cannot read settings: " << error.what() << '\n';
    }
    return settings;
}

void Settings::save() const {
    auto file = Glib::KeyFile::create();
    auto setStrings = [&](const char* key, const auto& values) {
        file->set_string_list(group, key, std::vector<Glib::ustring>(values.begin(), values.end()));
    };
    file->set_string(group, "dictionaryPath", dictionaryPath);
    setStrings("disabledDictionaries", disabled);
    setStrings("dictionaryOrder", order);
    setStrings("history", history);
    file->set_boolean(group, "startMinimized", startMinimized);
    file->set_boolean(group, "hideToMenuBar", hideToMenuBar);
    file->set_integer(group, "hotKeyCode", static_cast<int>(hotKey.keyCode));
    file->set_integer(group, "hotKeyModifiers", static_cast<int>(hotKey.modifiers));
    file->set_integer(group, "windowWidth", windowWidth);
    file->set_integer(group, "windowHeight", windowHeight);
    file->set_integer(group, "leftPane", leftPane);
    file->set_integer(group, "articlePane", articlePane);
    try {
        std::filesystem::create_directories(std::filesystem::path(settingsPath()).parent_path());
        file->save_to_file(settingsPath());
    } catch (const std::exception& error) {
        std::cerr << "Cannot save settings: " << error.what() << '\n';
    }
}
