#pragma once

#include "platform/platform.hpp"

#include <set>
#include <string>
#include <vector>

/// Everything that outlives a run, in one key file under the user's config folder.
/// Dictionary ids are .ifo paths relative to the dictionaries folder, so these settings survive
/// moving the folder. `order` keeps ids of absent books on purpose.
struct Settings {
    std::string dictionaryPath = "~/.dict";
    std::set<std::string> disabled;
    std::vector<std::string> order;
    platform::HotKey hotKey = platform::defaultHotKey();
    bool startMinimized = false;
    bool hideToMenuBar = false;
    std::vector<std::string> history;
    int windowWidth = 760;
    int windowHeight = 570;
    int leftPane = 200;
    int articlePane = 400;

    static Settings load();
    void save() const;
};
