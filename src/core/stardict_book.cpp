#include "core/stardict_book.hpp"

#include "core/text_folding.hpp"

#include <glib.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace fs = std::filesystem;

namespace ljdict {
namespace {

using Info = std::map<std::string, std::string, std::less<>>;

std::string_view trimAscii(std::string_view text) {
    constexpr std::string_view spaces = " \t\r";
    size_t start = text.find_first_not_of(spaces);
    if (start == std::string_view::npos) return {};
    return text.substr(start, text.find_last_not_of(spaces) - start + 1);
}

Info parseIfo(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();

    Info info;
    bool sawMagic = false;
    std::string_view rest = text;
    while (!rest.empty()) {
        size_t lineEnd = rest.find_first_of("\r\n");
        std::string_view line = rest.substr(0, lineEnd);
        rest = lineEnd == std::string_view::npos ? std::string_view{} : rest.substr(lineEnd + 1);
        if (line.empty()) continue;
        if (!sawMagic) {
            if (!line.ends_with("StarDict's dict ifo file")) throw DictionaryError("not a StarDict .ifo file");
            sawMagic = true;
            continue;
        }
        size_t separator = line.find('=');
        if (separator == std::string_view::npos) continue;
        info[std::string(trimAscii(line.substr(0, separator)))] = std::string(trimAscii(line.substr(separator + 1)));
    }
    if (!sawMagic) throw DictionaryError("not a StarDict .ifo file");
    return info;
}

std::string infoValue(const Info& info, std::string_view key) {
    auto found = info.find(key);
    return found == info.end() ? std::string{} : found->second;
}

uint64_t readBigEndian(const char* bytes, int count) {
    uint64_t value = 0;
    for (int i = 0; i < count; ++i) value = value << 8 | static_cast<unsigned char>(bytes[i]);
    return value;
}

fs::path withSuffix(fs::path base, const char* suffix) {
    base += suffix;
    return base;
}

}

void StarDictBook::MappedFileDeleter::operator()(GMappedFile* file) const {
    g_mapped_file_unref(file);
}

StarDictBook::MappedFile StarDictBook::map(const fs::path& path) {
    GError* error = nullptr;
    GMappedFile* file = g_mapped_file_new(path.c_str(), FALSE, &error);
    if (!file) {
        std::string message = error ? error->message : "cannot open file";
        g_clear_error(&error);
        throw DictionaryError(message);
    }
    return MappedFile(file);
}

std::string_view StarDictBook::bytes(const MappedFile& file) {
    return {g_mapped_file_get_contents(file.get()), g_mapped_file_get_length(file.get())};
}

StarDictBook::StarDictBook(const fs::path& ifo, std::string id) : id_(std::move(id)) {
    Info info = parseIfo(ifo);
    fs::path base = ifo;
    base.replace_extension();
    fs::path idx = withSuffix(base, ".idx");
    fs::path dict = withSuffix(base, ".dict");
    if (!fs::exists(idx)) {
        if (fs::exists(withSuffix(base, ".idx.gz"))) throw DictionaryError("compressed .idx.gz is not supported");
        throw DictionaryError("missing .idx");
    }
    if (!fs::exists(dict)) {
        if (fs::exists(withSuffix(base, ".dict.dz"))) throw DictionaryError("compressed .dict.dz is not supported");
        throw DictionaryError("missing .dict");
    }

    name_ = infoValue(info, "bookname");
    if (name_.empty()) name_ = base.filename().string();
    name_ = folding::validUtf8(name_);
    if (std::string sequence = infoValue(info, "sametypesequence"); !sequence.empty()) sameTypeSequence_ = sequence;
    indexFile_ = map(idx);
    articleFile_ = map(dict);

    int offsetBytes = infoValue(info, "idxoffsetbits") == "64" ? 8 : 4;
    size_t expectedCount = 0;
    try {
        expectedCount = std::stoull(infoValue(info, "wordcount"));
    } catch (const std::exception&) {
    }
    buildIndex(offsetBytes, expectedCount);
}

void StarDictBook::buildIndex(int offsetBytes, size_t expectedCount) {
    std::string_view data = bytes(indexFile_);
    keys_.reserve(data.size());
    entries_.reserve(expectedCount);

    size_t position = 0;
    while (position < data.size()) {
        auto found = static_cast<const char*>(memchr(data.data() + position, 0, data.size() - position));
        if (!found) throw DictionaryError("malformed .idx file");
        size_t wordEnd = found - data.data();
        size_t next = wordEnd + 1 + offsetBytes + 4;
        if (next > data.size() || wordEnd - position > std::numeric_limits<uint16_t>::max()) {
            throw DictionaryError("malformed .idx file");
        }

        size_t keyStart = keys_.size();
        folding::append(data.substr(position, wordEnd - position), keys_);
        entries_.push_back(Entry{
            .keyStart = static_cast<uint32_t>(keyStart),
            .wordStart = static_cast<uint32_t>(position),
            .keyLength = static_cast<uint16_t>(std::min<size_t>(keys_.size() - keyStart, std::numeric_limits<uint16_t>::max())),
            .wordLength = static_cast<uint16_t>(wordEnd - position),
            .size = static_cast<uint32_t>(readBigEndian(data.data() + wordEnd + 1 + offsetBytes, 4)),
            .offset = readBigEndian(data.data() + wordEnd + 1, offsetBytes),
        });
        position = next;
    }

    std::stable_sort(entries_.begin(), entries_.end(), [this](const Entry& a, const Entry& b) {
        return key(a) < key(b);
    });
}

int StarDictBook::compare(const Entry& entry, std::string_view prefix) const {
    std::string_view entryKey = key(entry);
    size_t common = std::min(entryKey.size(), prefix.size());
    int order = common == 0 ? 0 : memcmp(entryKey.data(), prefix.data(), common);
    if (order != 0) return order;
    return entryKey.size() < prefix.size() ? -1 : 0;
}

std::span<const StarDictBook::Entry> StarDictBook::entries(std::string_view prefix, size_t limit) const {
    size_t low = 0;
    size_t high = entries_.size();
    while (low < high) {
        size_t middle = (low + high) / 2;
        if (compare(entries_[middle], prefix) < 0) low = middle + 1; else high = middle;
    }
    size_t end = low;
    while (end < entries_.size() && end - low < limit && compare(entries_[end], prefix) == 0) ++end;
    return std::span(entries_).subspan(low, end - low);
}

std::vector<StarDictBook::Entry> StarDictBook::entriesWithKey(std::string_view key) const {
    std::vector<Entry> result;
    for (const Entry& entry : entries(key)) {
        if (entry.keyLength != key.size()) break;
        result.push_back(entry);
    }
    return result;
}

std::string StarDictBook::word(const Entry& entry) const {
    return folding::validUtf8(bytes(indexFile_).substr(entry.wordStart, entry.wordLength));
}

std::string_view StarDictBook::key(const Entry& entry) const {
    return std::string_view(keys_).substr(entry.keyStart, entry.keyLength);
}

std::vector<ArticlePart> StarDictBook::parts(const Entry& entry) const {
    std::string_view data = bytes(articleFile_);
    if (entry.offset > data.size() || entry.size > data.size() - entry.offset) return {};
    return parseRecord(data.substr(entry.offset, entry.size), sameTypeSequence_);
}

std::vector<ArticlePart> StarDictBook::parseRecord(std::string_view bytes, const std::optional<std::string>& sameTypeSequence) {
    std::vector<ArticlePart> parts;
    size_t position = 0;

    auto readField = [&](char type, bool isLast) {
        size_t end;
        size_t next;
        if (type >= 'a' && type <= 'z') {
            end = isLast ? bytes.size() : std::min(bytes.find('\0', position), bytes.size());
            next = end + 1;
        } else {
            if (position + 4 > bytes.size()) return false;
            size_t size = readBigEndian(bytes.data() + position, 4);
            position += 4;
            end = isLast ? bytes.size() : std::min(position + size, bytes.size());
            next = end;
        }
        std::string_view slice = bytes.substr(position, end - position);
        while (!slice.empty() && slice.back() == '\0') slice.remove_suffix(1);
        parts.push_back({type, folding::validUtf8(slice)});
        position = next;
        return true;
    };

    if (sameTypeSequence) {
        for (size_t index = 0; index < sameTypeSequence->size(); ++index) {
            if (position > bytes.size()) break;
            if (!readField((*sameTypeSequence)[index], index + 1 == sameTypeSequence->size())) break;
        }
    } else {
        while (position < bytes.size()) {
            char type = bytes[position++];
            if (!readField(type, false)) break;
        }
    }
    return parts;
}

}
