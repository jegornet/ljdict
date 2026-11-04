#include "core/article_renderer.hpp"

#include "core/text_folding.hpp"

#include <glib.h>

#include <array>
#include <cctype>
#include <cstring>
#include <vector>

namespace ljdict::ArticleRenderer {
namespace {

constexpr std::string_view stressMark = "\xCC\x81"; // U+0301

std::string replaceAll(std::string text, std::string_view from, std::string_view to) {
    for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size()) {
        text.replace(at, from.size(), to);
    }
    return text;
}

bool isWhitespaceAt(std::string_view text) {
    return !text.empty() && g_unichar_isspace(g_utf8_get_char(text.data()));
}

std::string_view dropLeadingSpace(std::string_view text) {
    while (isWhitespaceAt(text)) text.remove_prefix(g_utf8_next_char(text.data()) - text.data());
    return text;
}

void dropTrailingSpace(std::string& text) {
    while (!text.empty()) {
        const char* last = g_utf8_find_prev_char(text.data(), text.data() + text.size());
        if (!last || !g_unichar_isspace(g_utf8_get_char(last))) break;
        text.resize(last - text.data());
    }
}

/// Length of a tag such as [/com] or [m2] that survived the Lingvo-to-StarDict conversion inside
/// <nu> runs, or 0 if the bracket at the start of `text` is real text.
size_t dslTagLength(std::string_view text) {
    if (text.starts_with("[s]")) {
        size_t end = text.find("[/s]");
        if (end != std::string_view::npos && text.substr(0, end).find('\n') == std::string_view::npos) return end + 4;
    }
    size_t position = text.starts_with("[/") ? 2 : 1;
    std::string_view rest = text.substr(position);
    for (std::string_view name : {"com", "i", "b", "u", "p", "c", "lang", "m"}) {
        if (!rest.starts_with(name)) continue;
        size_t after = position + name.size();
        if (name == "m" && after < text.size() && std::isdigit(static_cast<unsigned char>(text[after]))) ++after;
        if (after < text.size() && text[after] == ']') return after + 1;
        if (after < text.size() && text[after] == ' ') {
            size_t close = text.find(']', after);
            if (close != std::string_view::npos) return close + 1;
        }
    }
    return 0;
}

std::string stripDslTags(std::string_view text) {
    std::string result;
    size_t position = 0;
    while (position < text.size()) {
        size_t bracket = text.find('[', position);
        if (bracket == std::string_view::npos) break;
        result.append(text.substr(position, bracket - position));
        size_t length = dslTagLength(text.substr(bracket));
        if (length == 0) result.push_back('[');
        position = bracket + std::max<size_t>(length, 1);
    }
    result.append(text.substr(position));
    return result;
}

/// [']X[/'] and its &apos; spelling become X with a stress mark.
std::string applyBareStress(std::string_view text) {
    static constexpr std::array<std::string_view, 2> opens = {"[']", "[&apos;]"};
    static constexpr std::array<std::string_view, 2> closes = {"[/']", "[/&apos;]"};
    std::string result;
    size_t position = 0;
    while (position < text.size()) {
        size_t bracket = text.find('[', position);
        if (bracket == std::string_view::npos) break;
        result.append(text.substr(position, bracket - position));
        position = bracket;
        std::string_view rest = text.substr(bracket);
        size_t consumed = 0;
        for (std::string_view open : opens) {
            if (!rest.starts_with(open) || rest.size() == open.size()) continue;
            std::string_view inner = rest.substr(open.size());
            size_t letter = g_utf8_next_char(inner.data()) - inner.data();
            if (letter > inner.size() || inner[0] == '\n') continue;
            for (std::string_view close : closes) {
                if (inner.substr(letter).starts_with(close)) {
                    result.append(inner.substr(0, letter)).append(stressMark);
                    consumed = open.size() + letter + close.size();
                }
            }
        }
        if (consumed == 0) result.push_back('[');
        position += std::max<size_t>(consumed, 1);
    }
    result.append(text.substr(position));
    return result;
}

struct Tag {
    std::string_view name;
    bool isClosing = false;
    bool isEmpty = false;
};

std::optional<Tag> parseTag(std::string_view inner) {
    Tag tag;
    tag.isClosing = inner.starts_with('/');
    if (tag.isClosing) inner.remove_prefix(1);
    tag.isEmpty = inner.ends_with('/');
    if (tag.isEmpty) inner.remove_suffix(1);
    size_t length = 0;
    while (length < inner.size() && !std::isspace(static_cast<unsigned char>(inner[length]))) ++length;
    tag.name = inner.substr(0, length);
    if (tag.name.empty()) return std::nullopt;
    for (char c : tag.name) {
        if (static_cast<unsigned char>(c) >= 0x80 || !std::isalnum(static_cast<unsigned char>(c))) return std::nullopt;
    }
    return tag;
}

bool isOneOf(std::string_view name, std::initializer_list<std::string_view> names) {
    for (std::string_view candidate : names) {
        if (name == candidate) return true;
    }
    return false;
}

class XDXFWriter {
public:
    void text(std::string_view raw) {
        if (raw.empty() || droppingResource_) return;
        if (inUnsorted_) {
            if (isOneOf(raw, {"'", "&apos;", "[']", "[&apos;]"})) {
                stressPending_ = true;
            } else if (!isOneOf(raw, {"[/']", "[/&apos;]"})) {
                append(raw.find('[') == std::string_view::npos ? std::string(raw) : stripDslTags(raw));
            }
        } else {
            append(raw.find('[') == std::string_view::npos ? std::string(raw) : applyBareStress(raw));
        }
    }

    void tag(const Tag& tag) {
        std::string_view name = tag.name;
        if (name == "nu") {
            inUnsorted_ = !inUnsorted_;
        } else if (name == "rref") {
            // Points at audio files that StarDict keeps elsewhere and these dictionaries do not ship.
            droppingResource_ = !tag.isClosing && !tag.isEmpty;
        } else if (name == "k") {
            block(tag.isClosing ? "</div>" : "<div class=\"k\">", tag.isClosing);
        } else if (name == "blockquote") {
            block(tag.isClosing ? "</blockquote>" : "<blockquote>", tag.isClosing);
        } else if (name == "tr") {
            output_ += tag.isClosing ? "]</span>" : "<span class=\"tr\">[";
        } else if (name == "kref" || name == "iref") {
            reference(tag);
        } else if (isOneOf(name, {"abr", "c", "co", "dtrn", "ex", "opt"})) {
            if (tag.isClosing) output_ += "</span>"; else output_.append("<span class=\"").append(name).append("\">");
        } else if (isOneOf(name, {"b", "i", "u", "sub", "sup"})) {
            output_.append(tag.isClosing ? "</" : "<").append(name).append(">");
        }
    }

    std::string finish() {
        while (!referenceStack_.empty()) {
            output_ = referenceStack_.back() + output_;
            referenceStack_.pop_back();
        }
        dropTrailingSpace(output_);
        return std::move(output_);
    }

private:
    void append(std::string_view text) {
        if (trimLeadingSpace_) {
            text = dropLeadingSpace(text);
            if (text.empty()) return;
            trimLeadingSpace_ = false;
        }
        if (stressPending_ && !text.empty() && g_unichar_isalpha(g_utf8_get_char(text.data()))) {
            stressPending_ = false;
            size_t letter = g_utf8_next_char(text.data()) - text.data();
            while (letter < text.size() && g_unichar_ismark(g_utf8_get_char(text.data() + letter))) {
                letter = g_utf8_next_char(text.data() + letter) - text.data();
            }
            output_.append(text.substr(0, letter)).append(stressMark);
            text.remove_prefix(letter);
        }
        output_.append(text);
    }

    /// Block elements already break the line, so the newlines around them would show twice.
    void block(std::string_view html, bool closing) {
        if (!closing) dropTrailingSpace(output_);
        output_.append(html);
        trimLeadingSpace_ = true;
    }

    void reference(const Tag& tag) {
        if (!tag.isClosing) {
            referenceStack_.push_back(std::move(output_));
            output_.clear();
        } else if (!referenceStack_.empty()) {
            std::string outer = std::move(referenceStack_.back());
            referenceStack_.pop_back();
            output_ = outer + link(output_, tag.name == "iref");
        }
    }

    static std::string link(const std::string& inner, bool external) {
        std::string target;
        for (size_t i = 0; i < inner.size(); ++i) {
            if (inner[i] != '<') {
                target.push_back(inner[i]);
                continue;
            }
            size_t close = inner.find('>', i);
            if (close == std::string::npos) {
                target.push_back(inner[i]);
            } else {
                i = close;
            }
        }
        target = replaceAll(std::move(target), stressMark, "");
        target = replaceAll(std::move(target), "&apos;", "'");
        target = replaceAll(std::move(target), "&quot;", "\"");
        target = replaceAll(std::move(target), "&lt;", "<");
        target = replaceAll(std::move(target), "&gt;", ">");
        target = replaceAll(std::move(target), "&amp;", "&");
        return "<a href=\"" + (external ? escape(target) : lookupURL(target)) + "\">" + inner + "</a>";
    }

    std::string output_;
    std::vector<std::string> referenceStack_;
    /// Lingvo text between a pair of <nu/> marks: unsorted words, stress marks, leftover DSL.
    bool inUnsorted_ = false;
    bool stressPending_ = false;
    bool droppingResource_ = false;
    bool trimLeadingSpace_ = true;
};

std::string headwordHTML(std::string_view word) {
    return "<div class=\"k\">" + escape(word) + "</div>";
}

std::optional<std::string> percentDecoded(std::string_view text) {
    std::string copy(text);
    char* decoded = g_uri_unescape_string(copy.c_str(), nullptr);
    if (!decoded) return std::nullopt;
    std::string result = folding::validUtf8(decoded);
    g_free(decoded);
    return result;
}

}

std::string html(const ArticlePart& part, std::string_view headword) {
    switch (part.type) {
    case 'x': {
        std::string body = xdxf(part.text);
        if (part.text.find("<k>") != std::string::npos) return body;
        return headwordHTML(headword) + "<div class=\"body\">" + body + "</div>";
    }
    case 'h':
    case 'g':
        return headwordHTML(headword) + part.text;
    case 'm':
    case 'l':
    case 't':
    case 'y':
    case 'k':
    case 'w':
        return headwordHTML(headword) + "<div class=\"body\">" + escape(part.text) + "</div>";
    default:
        return {};
    }
}

std::string escape(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        default: result.push_back(c);
        }
    }
    return result;
}

std::string lookupURL(std::string_view word) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string url = "ljdict://lookup?w=";
    for (char c : word) {
        auto byte = static_cast<unsigned char>(c);
        if (byte < 0x80 && std::isalnum(byte)) {
            url.push_back(c);
        } else {
            url.push_back('%');
            url.push_back(hex[byte >> 4]);
            url.push_back(hex[byte & 0xF]);
        }
    }
    return url;
}

std::optional<std::string> lookupWord(std::string_view url) {
    size_t colon = url.find(':');
    if (colon == std::string_view::npos) return std::nullopt;
    std::string scheme(url.substr(0, colon));
    for (char& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (scheme == "bword") {
        if (!url.substr(colon).starts_with("://")) return std::nullopt;
        return percentDecoded(url.substr(colon + 3));
    }
    if (scheme != "ljdict") return std::nullopt;
    size_t mark = url.find('?');
    if (mark == std::string_view::npos) return std::nullopt;
    std::string_view query = url.substr(mark + 1);
    query = query.substr(0, query.find('#'));
    while (!query.empty()) {
        size_t separator = query.find('&');
        std::string_view item = query.substr(0, separator);
        if (item.starts_with("w=")) return percentDecoded(item.substr(2));
        query = separator == std::string_view::npos ? std::string_view{} : query.substr(separator + 1);
    }
    return std::nullopt;
}

std::string xdxf(std::string_view source) {
    XDXFWriter writer;
    std::string_view rest = source;
    size_t open;
    while ((open = rest.find('<')) != std::string_view::npos) {
        writer.text(rest.substr(0, open));
        size_t close = rest.find('>', open);
        std::optional<Tag> tag;
        if (close != std::string_view::npos) tag = parseTag(rest.substr(open + 1, close - open - 1));
        if (!tag) {
            writer.text("&lt;");
            rest.remove_prefix(open + 1);
            continue;
        }
        writer.tag(*tag);
        rest.remove_prefix(close + 1);
    }
    writer.text(rest);
    return writer.finish();
}

}
