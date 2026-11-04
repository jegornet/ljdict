#pragma once

#include "core/stardict_book.hpp"

#include <optional>
#include <string>
#include <string_view>

/// Turns StarDict records into HTML. XDXF is converted by a tolerant tag scanner rather than an
/// XML parser because records are fragments with no root and are not always well-formed.
namespace ljdict::ArticleRenderer {

std::string html(const ArticlePart& part, std::string_view headword);
std::string escape(std::string_view text);
std::string lookupURL(std::string_view word);

/// The word a clicked article link points at, or nothing for anything else.
std::optional<std::string> lookupWord(std::string_view url);

std::string xdxf(std::string_view source);

}
