#include "core/dictionary_library.hpp"

#include "core/article_renderer.hpp"
#include "core/text_folding.hpp"

#include <glib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <thread>
#include <unordered_map>
#include <variant>

namespace fs = std::filesystem;

namespace ljdict {
namespace {

bool isIfo(const fs::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
    return extension == ".ifo";
}

/// Numbers inside ids sort by value, the way Finder sorts names.
std::string naturalKey(const std::string& id) {
    char* key = g_utf8_collate_key_for_filename(id.c_str(), -1);
    std::string result = key;
    g_free(key);
    return result;
}

}

std::shared_ptr<const DictionaryLibrary> DictionaryLibrary::load(const fs::path& requestedRoot) {
    std::error_code error;
    fs::path root = fs::canonical(requestedRoot, error);
    if (error) return std::make_shared<DictionaryLibrary>();

    std::vector<fs::path> ifoFiles;
    std::vector<std::string> ids;
    for (fs::recursive_directory_iterator walker(root, fs::directory_options::skip_permission_denied, error), end;
         !error && walker != end; walker.increment(error)) {
        if (!isIfo(walker->path())) continue;
        ifoFiles.push_back(walker->path());
        fs::path resolved = fs::weakly_canonical(walker->path(), error);
        ids.push_back(resolved.lexically_relative(root).generic_string());
        error.clear();
    }

    using Result = std::variant<std::monostate, std::shared_ptr<const StarDictBook>, std::string>;
    std::vector<Result> results(ifoFiles.size());
    std::atomic<size_t> nextIndex{0};
    auto work = [&] {
        for (size_t index; (index = nextIndex.fetch_add(1)) < ifoFiles.size();) {
            try {
                results[index] = std::make_shared<const StarDictBook>(ifoFiles[index], ids[index]);
            } catch (const std::exception& failure) {
                results[index] = std::string(failure.what());
            }
        }
    };
    std::vector<std::thread> workers;
    size_t workerCount = std::min<size_t>(ifoFiles.size(), std::max(1u, std::thread::hardware_concurrency()));
    for (size_t i = 0; i < workerCount; ++i) workers.emplace_back(work);
    for (auto& worker : workers) worker.join();

    auto library = std::make_shared<DictionaryLibrary>();
    for (size_t index = 0; index < results.size(); ++index) {
        if (auto* book = std::get_if<std::shared_ptr<const StarDictBook>>(&results[index])) {
            library->books_.push_back(std::move(*book));
        } else if (auto* reason = std::get_if<std::string>(&results[index])) {
            library->failures_.push_back({ids[index], std::move(*reason)});
        }
    }
    std::sort(library->books_.begin(), library->books_.end(), [](const auto& a, const auto& b) {
        return naturalKey(a->id()) < naturalKey(b->id());
    });
    std::sort(library->failures_.begin(), library->failures_.end(), [](const Failure& a, const Failure& b) {
        return a.path < b.path;
    });
    return library;
}

BookList DictionaryLibrary::booksInOrder(const std::vector<std::string>& order) const {
    std::unordered_map<std::string, size_t> rank;
    for (size_t index = 0; index < order.size(); ++index) rank[order[index]] = index;

    std::vector<std::pair<size_t, std::shared_ptr<const StarDictBook>>> ranked;
    for (size_t offset = 0; offset < books_.size(); ++offset) {
        auto found = rank.find(books_[offset]->id());
        ranked.emplace_back(found != rank.end() ? found->second : order.size() + offset, books_[offset]);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    BookList result;
    for (auto& item : ranked) result.push_back(std::move(item.second));
    return result;
}

std::vector<std::string> DictionaryLibrary::search(std::string_view query, const BookList& books, size_t limit) const {
    std::string valid = folding::validUtf8(query);
    std::string_view trimmed = folding::trim(valid);
    std::string prefix = folding::key(trimmed);
    if (prefix.empty()) return {};
    std::string typed = folding::rowKey(trimmed);

    struct Row {
        std::string word;
        std::string rowKey;
        std::string_view key;
    };
    std::unordered_map<std::string, Row> rows;
    for (const auto& book : books) {
        // Each book's first `limit` entries are enough: the overall first `limit` are among them.
        for (const auto& entry : book->entries(prefix, limit)) {
            std::string word = book->word(entry);
            std::string rowKey = folding::rowKey(word);
            if (!rows.contains(rowKey)) rows.emplace(rowKey, Row{std::move(word), rowKey, book->key(entry)});
        }
    }

    std::vector<const Row*> sorted;
    sorted.reserve(rows.size());
    for (const auto& [_, row] : rows) sorted.push_back(&row);
    std::sort(sorted.begin(), sorted.end(), [&](const Row* a, const Row* b) {
        bool aExact = a->rowKey == typed;
        bool bExact = b->rowKey == typed;
        if (aExact != bExact) return aExact;
        if (a->key != b->key) return a->key < b->key;
        return a->rowKey < b->rowKey;
    });

    std::vector<std::string> words;
    for (size_t i = 0; i < sorted.size() && i < limit; ++i) words.push_back(sorted[i]->word);
    return words;
}

std::vector<Article> DictionaryLibrary::articles(std::string_view word, const BookList& books) const {
    std::string key = folding::key(word);
    std::string rowKey = folding::rowKey(word);
    std::vector<Article> articles;
    for (const auto& book : books) {
        for (const auto& entry : book->entriesWithKey(key)) {
            std::string headword = book->word(entry);
            if (folding::rowKey(headword) != rowKey) continue;
            std::string html;
            for (const ArticlePart& part : book->parts(entry)) html += ArticleRenderer::html(part, headword);
            if (!html.empty()) articles.push_back({book->name(), std::move(html)});
        }
    }
    return articles;
}

}
