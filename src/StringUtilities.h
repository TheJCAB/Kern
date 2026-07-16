#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Configure the console to admit and emit UTF-8 text.
// No-op on Linux, actively configures the console on Windows.
// Optimistically registers atexit to undo the console change.
// (A bona-fide crash will fail to run the atexit handler)
void ConfigureConsoleForUtf8();

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
