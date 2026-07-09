#pragma once

#include <string>
#include <string_view>

// Escape special characters and quotes to allow any arbitrary string to be quotable.
std::string EscapeString(std::string_view const& input);

// Undo the string escaping to recover the original string.
std::string UnescapeString(std::string_view const& input);

// Return a printable, width-constrained version of the given string.
std::string OneLine(std::string_view, size_t characterLimit);

// Find and replace all newlines (regardless of newline encoding) with the given string.
void ReplaceNewlinesIn(std::string& string, std::string_view replacement);

// Find and replace in the given string.
void ReplaceIn(std::string& string, std::string_view placeholder, std::string_view replacement);
