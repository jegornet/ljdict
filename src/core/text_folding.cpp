#include "core/text_folding.hpp"

#include <glib.h>

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ljdict::folding {
namespace {

constexpr gunichar tableStart = 0x80;
constexpr gunichar tableEnd = 0x2500;

struct GFreeDeleter {
    void operator()(char* text) const { g_free(text); }
};
using GString_ = std::unique_ptr<char, GFreeDeleter>;

/// Letters that have no canonical decomposition, so stripping combining marks would miss them.
const char* overrideFor(gunichar scalar) {
    static const std::unordered_map<gunichar, const char*> overrides = {
        {U'đ', "d"}, {U'ð', "d"}, {U'ø', "o"}, {U'ł', "l"}, {U'ħ', "h"}, {U'ı', "i"}, {U'ŧ', "t"},
        {U'ß', "ss"}, {U'æ', "ae"}, {U'œ', "oe"}, {U'þ', "th"}, {U'’', "'"},
        {U'ү', "у"}, {U'ұ', "у"}, {U'ө', "о"}, {U'ң', "н"}, {U'қ', "к"}, {U'ғ', "г"}, {U'җ', "ж"}, {U'һ', "х"},
    };
    auto found = overrides.find(scalar);
    return found == overrides.end() ? nullptr : found->second;
}

void appendScalar(gunichar scalar, std::string& out) {
    std::array<char, 6> bytes;
    out.append(bytes.data(), g_unichar_to_utf8(scalar, bytes.data()));
}

std::string foldSlowly(gunichar scalar) {
    std::array<char, 6> bytes;
    gssize length = g_unichar_to_utf8(scalar, bytes.data());
    GString_ lower(g_utf8_strdown(bytes.data(), length));
    GString_ decomposed(g_utf8_normalize(lower.get(), -1, G_NORMALIZE_NFD));
    std::string result;
    if (!decomposed) return result;
    for (const char* p = decomposed.get(); *p; p = g_utf8_next_char(p)) {
        gunichar part = g_utf8_get_char(p);
        if (g_unichar_type(part) == G_UNICODE_NON_SPACING_MARK) continue;
        if (const char* replacement = overrideFor(part)) {
            result += replacement;
        } else {
            appendScalar(part, result);
        }
    }
    return result;
}

/// UTF-8 of the folded form for each scalar in the table's range. Empty for combining marks.
const std::vector<std::string>& table() {
    static const std::vector<std::string> folded = [] {
        std::vector<std::string> result(tableEnd - tableStart);
        for (gunichar value = tableStart; value < tableEnd; ++value) {
            if (g_unichar_validate(value)) result[value - tableStart] = foldSlowly(value);
        }
        return result;
    }();
    return folded;
}

void appendValid(std::string_view word, std::string& key) {
    const auto& folded = table();
    const char* p = word.data();
    const char* end = p + word.size();
    while (p < end) {
        auto byte = static_cast<unsigned char>(*p);
        if (byte < 0x80) {
            key.push_back(static_cast<char>(byte >= 'A' && byte <= 'Z' ? byte + 0x20 : byte));
            ++p;
            continue;
        }
        gunichar scalar = g_utf8_get_char(p);
        const char* next = g_utf8_next_char(p);
        if (scalar >= tableStart && scalar < tableEnd) {
            key += folded[scalar - tableStart];
        } else {
            key.append(p, next - p);
        }
        p = next;
    }
}

}

std::string validUtf8(std::string_view bytes) {
    if (g_utf8_validate_len(bytes.data(), bytes.size(), nullptr)) return std::string(bytes);
    GString_ repaired(g_utf8_make_valid(bytes.data(), bytes.size()));
    return repaired.get();
}

void append(std::string_view word, std::string& key) {
    if (g_utf8_validate_len(word.data(), word.size(), nullptr)) {
        appendValid(word, key);
    } else {
        appendValid(validUtf8(word), key);
    }
}

std::string key(std::string_view word) {
    std::string result;
    result.reserve(word.size());
    append(word, result);
    return result;
}

std::string rowKey(std::string_view word) {
    std::string valid = validUtf8(word);
    GString_ lower(g_utf8_strdown(valid.data(), valid.size()));
    GString_ composed(g_utf8_normalize(lower.get(), -1, G_NORMALIZE_NFC));
    return composed ? composed.get() : lower.get();
}

std::string_view trim(std::string_view text) {
    const char* start = text.data();
    const char* end = start + text.size();
    while (start < end && g_unichar_isspace(g_utf8_get_char(start))) start = g_utf8_next_char(start);
    while (end > start) {
        const char* previous = g_utf8_find_prev_char(start, end);
        if (!previous || !g_unichar_isspace(g_utf8_get_char(previous))) break;
        end = previous;
    }
    return {start, static_cast<size_t>(end - start)};
}

}
