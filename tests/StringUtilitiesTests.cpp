
#include "StringUtilities.h"

#include "TestUtilities.h"

#include <cstdint>
#include <iostream>
#include <print>
#include <string>
#include <vector>

int main()
{
    std::println("Test EscapeString and UnescapeString");
    std::string original  = "Hello \"World\"! \n New line.";
    std::string escaped   = EscapeString(original);
    std::string unescaped = UnescapeString(escaped);
    ExpectEqual(original, unescaped, "expected unescaping recovers an escaped string");

    std::println("Test OneLine");
    std::string multiLine = "Line 1\nLine 2\nLine 3";
    std::string oneLine   = OneLine(multiLine, 10);
    // It should be at most 10 characters long.
    Expect(oneLine.length() <= 10 + 1 + 3, "OneLine must return a size-limited string"); // Adds the "\n" and "...".

    std::println("Test ReplaceNewlinesIn");
    std::string nlString = "Line 1\nLine 2\r\nLine 3";
    ReplaceNewlinesIn(nlString, " ");
    ExpectEqual(nlString, "Line 1 Line 2 Line 3", "ReplaceNewlinesIn must find and replace all newlines");

    std::println("Test CountLines");
    ExpectEqual(CountLines(""), std::int64_t{0}, "CountLines must return 0 for empty text");
    ExpectEqual(CountLines("a\nb\nc"), std::int64_t{3}, "CountLines must count lines without a trailing newline");
    ExpectEqual(CountLines("a\nb\nc\n"), std::int64_t{3}, "CountLines must not count a trailing newline as a line");

    std::println("Test ReplaceIn");
    std::string replaceStr = "The quick brown fox";
    ReplaceIn(replaceStr, "brown", "red");
    ExpectEqual(replaceStr, "The quick red fox", "ReplaceIn must replace the given string");

    std::println("Test SplitLines and JoinLines");
    std::vector<std::string> const splitLines = SplitLines("Line 1\nLine 2\nLine 3\n");
    ExpectEqual(splitLines.size(), std::size_t{3}, "SplitLines must not produce a trailing empty line");
    ExpectEqual(splitLines.front(), std::string("Line 1"), "SplitLines must keep the first line");
    ExpectEqual(splitLines.back(), std::string("Line 3"), "SplitLines must keep the last line");
    Expect(SplitLines("").empty(), "SplitLines must return no lines for empty text");
    ExpectEqual(JoinLines(splitLines), std::string("Line 1\nLine 2\nLine 3"), "JoinLines must not add a trailing newline");
    ExpectEqual(JoinLines(std::vector<std::string>{}), std::string(""), "JoinLines must return an empty string for no lines");

    // Test ConfigureConsoleForUtf8 (hard to test without a real console, but we can check it doesn't crash)
    ConfigureConsoleForUtf8();

    std::println("All tests passed!");
    return 0;
}