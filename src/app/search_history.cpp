#include "app/search_history.hpp"

#include <algorithm>

void SearchHistory::add(const std::string& word) {
    auto& words = settings_.history;
    std::erase(words, word);
    words.insert(words.begin(), word);
    if (words.size() > limit) words.resize(limit);
    settings_.save();
}

void SearchHistory::remove(const std::string& word) {
    std::erase(settings_.history, word);
    settings_.save();
}

void SearchHistory::clear() {
    settings_.history.clear();
    settings_.save();
}
