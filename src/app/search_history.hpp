#pragma once

#include "app/settings.hpp"

#include <string>
#include <vector>

/// Most recent word first, stored with the settings.
class SearchHistory {
public:
    static constexpr size_t limit = 500;

    explicit SearchHistory(Settings& settings) : settings_(settings) {}

    const std::vector<std::string>& words() const { return settings_.history; }
    void add(const std::string& word);
    void remove(const std::string& word);
    void clear();

private:
    Settings& settings_;
};
