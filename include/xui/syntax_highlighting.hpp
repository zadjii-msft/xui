#pragma once
#include "xui/documents.hpp"
#include <string_view>

namespace xui {
bool syntax_highlighting_available() noexcept;
// An empty language disables highlighting. Unknown language IDs are errors.
void set_syntax_language(MultilineText& document, std::string_view language);
// Empty means no supported grammar. Does not read the file or load its contents as a grammar.
std::string_view syntax_language_for_path(std::wstring_view path);
}
