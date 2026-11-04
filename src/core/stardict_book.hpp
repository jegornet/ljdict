#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

typedef struct _GMappedFile GMappedFile;

namespace ljdict {

struct DictionaryError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// One typed piece of a StarDict record.
struct ArticlePart {
    char type;
    std::string text;
    bool operator==(const ArticlePart&) const = default;
};

/// A StarDict dictionary (.ifo + .idx + .dict). Immutable after construction, so it is safe to share.
class StarDictBook {
public:
    struct Entry {
        uint32_t keyStart;
        uint32_t wordStart;
        uint16_t keyLength;
        uint16_t wordLength;
        uint32_t size;
        uint64_t offset;
    };

    StarDictBook(const std::filesystem::path& ifo, std::string id);

    /// Path relative to the dictionaries folder; stays valid if that folder moves.
    const std::string& id() const { return id_; }
    const std::string& name() const { return name_; }
    size_t wordCount() const { return entries_.size(); }

    /// Entries whose folded key starts with `prefix`, in key order.
    std::span<const Entry> entries(std::string_view prefix, size_t limit = SIZE_MAX) const;
    /// Entries with exactly this folded key.
    std::vector<Entry> entriesWithKey(std::string_view key) const;

    std::string word(const Entry& entry) const;
    std::string_view key(const Entry& entry) const;
    std::vector<ArticlePart> parts(const Entry& entry) const;

    /// Lowercase types are NUL-terminated (or run to the end of the record),
    /// uppercase ones are prefixed with a big-endian 32-bit size.
    static std::vector<ArticlePart> parseRecord(std::string_view bytes, const std::optional<std::string>& sameTypeSequence);

private:
    struct MappedFileDeleter {
        void operator()(GMappedFile* file) const;
    };
    using MappedFile = std::unique_ptr<GMappedFile, MappedFileDeleter>;

    static MappedFile map(const std::filesystem::path& path);
    static std::string_view bytes(const MappedFile& file);
    void buildIndex(int offsetBytes, size_t expectedCount);
    /// Orders an entry's key against `prefix`, treating keys that start with `prefix` as equal.
    int compare(const Entry& entry, std::string_view prefix) const;

    std::string id_;
    std::string name_;
    std::optional<std::string> sameTypeSequence_;
    MappedFile indexFile_;
    MappedFile articleFile_;
    std::string keys_;
    std::vector<Entry> entries_;
};

}
