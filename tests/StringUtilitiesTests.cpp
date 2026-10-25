
#include "StringUtilities.h"

#include "TestUtilities.h"

#include <iostream>
#include <string>

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

    std::println("Test ReplaceIn");
    std::string replaceStr = "The quick brown fox";
    ReplaceIn(replaceStr, "brown", "red");
    ExpectEqual(replaceStr, "The quick red fox", "ReplaceIn must replace the given string");

    // Test ConfigureConsoleForUtf8 (hard to test without a real console, but we can check it doesn't crash)
    ConfigureConsoleForUtf8();

    std::println("All tests passed!");
    return 0;
}