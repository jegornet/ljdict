#include "core/rich_text.hpp"

#include "core/text_folding.hpp"

#include <glib.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <optional>

namespace ljdict {
namespace {

bool isAny(std::string_view name, std::initializer_list<std::string_view> names) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool isVoid(std::string_view name) {
    return isAny(name, {"br", "hr", "img", "meta", "link", "input", "wbr", "area", "base", "col", "embed", "param", "source", "track"});
}

bool isBlock(std::string_view name) {
    return isAny(name, {"div", "p", "blockquote", "ul", "ol", "li", "table", "tr", "h1", "h2", "h3", "h4", "h5", "h6",
                        "pre", "center", "dl", "dt", "dd", "hr", "section", "article"});
}

/// Their content is not text for the reader.
bool isDropped(std::string_view name) {
    return isAny(name, {"script", "style", "head", "title", "object", "iframe", "template", "noscript"});
}

std::string lowercased(std::string_view text) {
    std::string result(text);
    for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

struct ParsedTag {
    std::string name;
    bool isClosing = false;
    bool isSelfClosing = false;
    std::map<std::string, std::string> attributes;
    size_t length = 0;
};

/// Nil when the `<` at the start of `html` does not open a tag, so it is plain text.
std::optional<ParsedTag> parseTag(std::string_view html) {
    ParsedTag tag;
    size_t i = 1;
    if (i < html.size() && html[i] == '/') {
        tag.isClosing = true;
        ++i;
    }
    if (i >= html.size() || !std::isalpha(static_cast<unsigned char>(html[i]))) return std::nullopt;
    size_t nameStart = i;
    while (i < html.size() && (std::isalnum(static_cast<unsigned char>(html[i])) || html[i] == ':' || html[i] == '-')) ++i;
    tag.name = lowercased(html.substr(nameStart, i - nameStart));

    while (i < html.size()) {
        char c = html[i];
        if (c == '>') {
            tag.length = i + 1;
            return tag;
        }
        if (c == '/') {
            tag.isSelfClosing = i + 1 < html.size() && html[i + 1] == '>';
            ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        size_t attributeStart = i;
        while (i < html.size() && !std::isspace(static_cast<unsigned char>(html[i])) && !isAny(html.substr(i, 1), {"=", ">", "/"})) ++i;
        std::string attribute = lowercased(html.substr(attributeStart, i - attributeStart));
        while (i < html.size() && std::isspace(static_cast<unsigned char>(html[i]))) ++i;
        std::string value;
        if (i < html.size() && html[i] == '=') {
            ++i;
            while (i < html.size() && std::isspace(static_cast<unsigned char>(html[i]))) ++i;
            if (i < html.size() && (html[i] == '"' || html[i] == '\'')) {
                size_t close = html.find(html[i], i + 1);
                if (close == std::string_view::npos) return std::nullopt;
                value = std::string(html.substr(i + 1, close - i - 1));
                i = close + 1;
            } else {
                size_t valueStart = i;
                while (i < html.size() && !std::isspace(static_cast<unsigned char>(html[i])) && html[i] != '>') ++i;
                value = std::string(html.substr(valueStart, i - valueStart));
            }
        }
        if (!attribute.empty()) tag.attributes.emplace(std::move(attribute), std::move(value));
    }
    return std::nullopt;
}

void appendScalar(gunichar scalar, std::string& out) {
    char bytes[6];
    out.append(bytes, g_unichar_to_utf8(scalar, bytes));
}

std::string decodeEntities(std::string_view text) {
    static const std::map<std::string_view, std::string_view> named = {
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", "\xC2\xA0"},
    };
    std::string result;
    size_t position = 0;
    while (position < text.size()) {
        size_t amp = text.find('&', position);
        if (amp == std::string_view::npos) break;
        result.append(text.substr(position, amp - position));
        position = amp + 1;
        size_t end = text.find(';', amp);
        if (end == std::string_view::npos || end - amp > 10) {
            result.push_back('&');
            continue;
        }
        std::string_view name = text.substr(amp + 1, end - amp - 1);
        if (auto found = named.find(name); found != named.end()) {
            result.append(found->second);
            position = end + 1;
        } else if (name.starts_with('#') && name.size() > 1) {
            bool isHex = name[1] == 'x' || name[1] == 'X';
            std::string digits(name.substr(isHex ? 2 : 1));
            char* parsedEnd = nullptr;
            unsigned long scalar = std::strtoul(digits.c_str(), &parsedEnd, isHex ? 16 : 10);
            if (!digits.empty() && *parsedEnd == '\0' && scalar != 0 && g_unichar_validate(static_cast<gunichar>(scalar))) {
                appendScalar(static_cast<gunichar>(scalar), result);
                position = end + 1;
            } else {
                result.push_back('&');
            }
        } else {
            result.push_back('&');
        }
    }
    result.append(text.substr(position));
    return result;
}

unsigned stylesForClasses(std::string_view classes) {
    unsigned styles = 0;
    while (!classes.empty()) {
        size_t end = classes.find(' ');
        std::string_view name = classes.substr(0, end);
        if (name == "k") styles |= Headword;
        else if (name == "tr") styles |= Transcription;
        else if (name == "abr") styles |= Abbreviation;
        else if (name == "ex") styles |= Example;
        else if (name == "co" || name == "opt") styles |= Comment;
        classes = end == std::string_view::npos ? std::string_view{} : classes.substr(end + 1);
    }
    return styles;
}

struct InlineStyle {
    std::string color;
    bool hasIndent = false;
};

/// `style` attributes in the data set only a text color and a left margin. The rest of CSS has no
/// counterpart in the text view, so it is dropped.
InlineStyle parseInlineStyle(std::string_view style) {
    InlineStyle result;
    while (!style.empty()) {
        size_t end = style.find(';');
        std::string_view declaration = style.substr(0, end);
        style = end == std::string_view::npos ? std::string_view{} : style.substr(end + 1);
        size_t colon = declaration.find(':');
        if (colon == std::string_view::npos) continue;
        std::string property = lowercased(folding::trim(declaration.substr(0, colon)));
        std::string_view value = folding::trim(declaration.substr(colon + 1));
        if (property == "color" && !value.empty()
            && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isalnum(c) || c == '#'; })) {
            result.color = std::string(value);
        } else if (property == "margin-left") {
            result.hasIndent = std::strtod(std::string(value).c_str(), nullptr) > 0;
        }
    }
    return result;
}

class Layout {
public:
    void text(std::string_view text) {
        if (text.empty()) return;
        const Open& state = open_.back();
        if (!spans_.empty() && spans_.back().styles == state.styles && spans_.back().indent == state.indent
            && spans_.back().href == state.href && spans_.back().color == state.color) {
            spans_.back().text += text;
        } else {
            spans_.push_back({std::string(text), state.styles, state.indent, state.href, state.color});
        }
    }

    /// Ends the current line unless nothing is on it. Called outside the block's own style so
    /// that the newline does not take on a headword's height.
    void breakLine() {
        if (!spans_.empty() && !spans_.back().text.ends_with('\n')) text("\n");
    }

    void openTag(const ParsedTag& tag) {
        if (isBlock(tag.name)) breakLine();
        if (tag.name == "br") text("\n");
        if (isVoid(tag.name)) return;

        Open state = open_.back();
        state.name = tag.name;
        state.styles |= stylesFor(tag);
        if (isAny(tag.name, {"blockquote", "ul", "ol", "dd"})) ++state.indent;
        if (tag.name == "a") {
            auto href = tag.attributes.find("href");
            if (href != tag.attributes.end()) state.href = decodeEntities(href->second);
        }
        if (auto style = tag.attributes.find("style"); style != tag.attributes.end()) {
            InlineStyle inlineStyle = parseInlineStyle(style->second);
            if (!inlineStyle.color.empty()) state.color = std::move(inlineStyle.color);
            if (inlineStyle.hasIndent) ++state.indent;
        }
        if (tag.isSelfClosing) return;
        open_.push_back(std::move(state));
        if (tag.name == "li") text("• ");
    }

    void closeTag(const std::string& name) {
        auto found = std::find_if(open_.rbegin(), open_.rend(), [&](const Open& state) { return state.name == name; });
        if (found == open_.rend() || found == std::prev(open_.rend())) return;
        open_.erase(std::prev(found.base()), open_.end());
        if (isBlock(name)) breakLine();
        if (name == "td" || name == "th") text(" ");
    }

    std::vector<RichSpan> finish() {
        while (!spans_.empty()) {
            std::string& last = spans_.back().text;
            last.erase(last.find_last_not_of(" \t\r\n") + 1);
            if (!last.empty()) break;
            spans_.pop_back();
        }
        return std::move(spans_);
    }

private:
    struct Open {
        std::string name;
        unsigned styles = 0;
        int indent = 0;
        std::string href;
        std::string color;
    };

    static unsigned stylesFor(const ParsedTag& tag) {
        const std::string& name = tag.name;
        unsigned styles = 0;
        if (isAny(name, {"b", "strong", "h1", "h2", "h3", "h4", "h5", "h6"})) styles |= Bold;
        if (isAny(name, {"i", "em"})) styles |= Italic;
        if (isAny(name, {"u", "ins"})) styles |= Underline;
        if (name == "sub") styles |= Subscript;
        if (name == "sup") styles |= Superscript;
        if (auto classes = tag.attributes.find("class"); classes != tag.attributes.end()) {
            styles |= stylesForClasses(classes->second);
        }
        return styles;
    }

    std::vector<Open> open_ = {Open{}};
    std::vector<RichSpan> spans_;
};

/// Offset just past the end tag of a dropped element, or the end of the input if it has none.
size_t skipDropped(std::string_view html, size_t from, const std::string& name) {
    std::string end = "</" + name;
    for (size_t i = from; i + end.size() <= html.size(); ++i) {
        if (html[i] != '<' || lowercased(html.substr(i, end.size())) != end) continue;
        size_t close = html.find('>', i);
        return close == std::string_view::npos ? html.size() : close + 1;
    }
    return html.size();
}

}

std::vector<RichSpan> layoutHTML(std::string_view html) {
    Layout layout;
    size_t position = 0;
    while (position < html.size()) {
        size_t open = html.find('<', position);
        if (open == std::string_view::npos) break;
        layout.text(decodeEntities(html.substr(position, open - position)));
        std::string_view rest = html.substr(open);

        if (rest.starts_with("<!--")) {
            size_t end = rest.find("-->");
            position = end == std::string_view::npos ? html.size() : open + end + 3;
        } else if (rest.starts_with("<!") || rest.starts_with("<?")) {
            size_t end = rest.find('>');
            position = end == std::string_view::npos ? html.size() : open + end + 1;
        } else if (auto tag = parseTag(rest)) {
            position = open + tag->length;
            if (tag->isClosing) {
                layout.closeTag(tag->name);
            } else if (isDropped(tag->name)) {
                if (!tag->isSelfClosing) position = skipDropped(html, position, tag->name);
            } else {
                layout.openTag(*tag);
            }
        } else {
            layout.text("<");
            position = open + 1;
        }
    }
    layout.text(decodeEntities(html.substr(std::min(position, html.size()))));
    return layout.finish();
}

}
