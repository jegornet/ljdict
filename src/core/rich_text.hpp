#pragma once

#include <string>
#include <string_view>
#include <vector>

/// Lays out article HTML as styled text. Dictionary content is untrusted and `h` records are raw
/// HTML, so only text, a fixed set of styles and link targets come out: there is no markup engine
/// behind this, and nothing can run or load.
namespace ljdict {

enum Style : unsigned {
    Bold = 1 << 0,
    Italic = 1 << 1,
    Underline = 1 << 2,
    Subscript = 1 << 3,
    Superscript = 1 << 4,
    Headword = 1 << 5,
    Transcription = 1 << 6,
    Abbreviation = 1 << 7,
    Example = 1 << 8,
    Comment = 1 << 9,
};

struct RichSpan {
    std::string text;
    unsigned styles = 0;
    int indent = 0;
    std::string href;
    /// From an inline style, as written there. Only letters, digits and `#`.
    std::string color;
    bool operator==(const RichSpan&) const = default;
};

std::vector<RichSpan> layoutHTML(std::string_view html);

}
