#include "app/app_model.hpp"

#include "core/text_folding.hpp"

#include <glibmm.h>

#include <algorithm>
#include <thread>

using namespace ljdict;

AppModel::AppModel() : settings_(Settings::load()), library_(std::make_shared<DictionaryLibrary>()) {}

BookList AppModel::orderedBooks() const {
    return library_->booksInOrder(settings_.order);
}

BookList AppModel::enabledBooks() const {
    BookList books = orderedBooks();
    std::erase_if(books, [this](const auto& book) { return !isEnabled(*book); });
    return books;
}

void AppModel::setDictionaryPath(const std::string& path) {
    std::string trimmed(folding::trim(path));
    if (trimmed.empty() || trimmed == settings_.dictionaryPath) return;
    settings_.dictionaryPath = trimmed;
    settings_.save();
    reload();
}

void AppModel::reload() {
    int generation = ++loadGeneration_;
    std::string root = settings_.dictionaryPath;
    if (root == "~" || root.starts_with("~/")) root = Glib::get_home_dir() + root.substr(1);
    isLoading_ = true;
    libraryChanged.emit();
    std::thread([this, generation, root] {
        auto library = DictionaryLibrary::load(root);
        Glib::MainContext::get_default()->invoke([this, generation, library] {
            if (generation == loadGeneration_) {
                library_ = library;
                isLoading_ = false;
                libraryChanged.emit();
            }
            return false;
        });
    }).detach();
}

bool AppModel::isEnabled(const StarDictBook& book) const {
    return !settings_.disabled.contains(book.id());
}

void AppModel::setEnabled(bool enabled, const StarDictBook& book) {
    if (enabled) settings_.disabled.erase(book.id()); else settings_.disabled.insert(book.id());
    settings_.save();
    libraryChanged.emit();
}

void AppModel::moveBook(size_t source, size_t destination) {
    std::vector<std::string> ids;
    for (const auto& book : orderedBooks()) ids.push_back(book->id());
    if (source >= ids.size() || destination > ids.size()) return;
    std::string moved = ids[source];
    ids.erase(ids.begin() + source);
    ids.insert(ids.begin() + (destination > source ? destination - 1 : destination), moved);
    // Books from another folder stay in the list so their place survives switching back.
    for (const std::string& id : settings_.order) {
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    }
    settings_.order = std::move(ids);
    settings_.save();
    libraryChanged.emit();
}
