#pragma once

#include "app/settings.hpp"
#include "core/dictionary_library.hpp"

#include <sigc++/sigc++.h>

#include <memory>
#include <string>

/// App-wide state: the loaded dictionaries and the settings.
class AppModel {
public:
    AppModel();

    Settings& settings() { return settings_; }
    const ljdict::DictionaryLibrary& library() const { return *library_; }
    bool isLoading() const { return isLoading_; }
    const std::string& dictionaryPath() const { return settings_.dictionaryPath; }

    ljdict::BookList orderedBooks() const;
    ljdict::BookList enabledBooks() const;

    void setDictionaryPath(const std::string& path);
    void reload();
    bool isEnabled(const ljdict::StarDictBook& book) const;
    void setEnabled(bool enabled, const ljdict::StarDictBook& book);
    /// Moves a book so that it comes before the one now at `destination`.
    void moveBook(size_t source, size_t destination);

    /// Fires on the main thread when the set of searchable dictionaries changes.
    sigc::signal<void()> libraryChanged;

private:
    Settings settings_;
    std::shared_ptr<const ljdict::DictionaryLibrary> library_;
    bool isLoading_ = false;
    int loadGeneration_ = 0;
};
