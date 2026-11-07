#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

// Count the number of lines in the given text. Empty text has no lines, and a
// trailing newline does not add an extra line.
std::int64_t CountLines(std::string_view const text);

// Split the given text into its lines. A trailing newline does not produce a
// final empty line, and empty text produces no lines.
std::vector<std::string> SplitLines(std::string_view const content);

// Join the given lines with newlines, without a trailing newline. No lines
// produces an empty string.
std::string JoinLines(std::vector<std::string> const& lines);
