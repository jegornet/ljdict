#pragma once

#include "core/stardict_book.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ljdict {

struct Article {
    std::string bookName;
    std::string html;
};

using BookList = std::vector<std::shared_ptr<const StarDictBook>>;

/// Every dictionary found under one folder, plus the ones that could not be opened.
class DictionaryLibrary {
public:
    struct Failure {
        std::string path;
        std::string reason;
    };

    DictionaryLibrary() = default;

    /// Opens every .ifo under `root`, recursively. Blocks; call off the main thread.
    static std::shared_ptr<const DictionaryLibrary> load(const std::filesystem::path& root);

    const BookList& books() const { return books_; }
    const std::vector<Failure>& failures() const { return failures_; }

    /// Books listed in `order` first, in that order; the rest keep their place after them.
    BookList booksInOrder(const std::vector<std::string>& order) const;

    /// Distinct headwords whose folded form starts with the folded query.
    /// An exact match of what was typed goes first, then key order.
    std::vector<std::string> search(std::string_view query, const BookList& books, size_t limit = 500) const;

    /// Articles for one headword, in dictionary order.
    std::vector<Article> articles(std::string_view word, const BookList& books) const;

private:
    BookList books_;
    std::vector<Failure> failures_;
};

}
