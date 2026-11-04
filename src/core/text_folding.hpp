#pragma once

#include <string>
#include <string_view>

/// Reduces words to a search key: lowercase, without diacritics. "Čim", "ćim" and "cim" share a key.
namespace ljdict::folding {

void append(std::string_view word, std::string& key);
std::string key(std::string_view word);

/// Identity of a word list row: letter case is merged, diacritics stay distinct.
std::string rowKey(std::string_view word);

/// Dictionary bytes are untrusted, and GLib's Unicode functions require valid UTF-8.
std::string validUtf8(std::string_view bytes);

/// Removes Unicode whitespace from the ends.
std::string_view trim(std::string_view text);

}
