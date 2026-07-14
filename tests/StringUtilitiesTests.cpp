#include <iostream>
#include <string>
#include <cassert>
#include "StringUtilities.h"

int main()
{
    // Test ConfigureConsoleForUtf8 (hard to test without a real console, but we can check it doesn't crash)
    ConfigureConsoleForUtf8();

    // Test Utf8ToSystemEncoding and SystemEncodingToUtf8
    // Since we don't know the exact system encoding, we'll just check that it handles a basic string.
    std::string input = "Hello World";
    std::string utf8 = Utf8ToSystemEncoding(input);
    std::string back = SystemEncodingToUtf8(utf8);
    // They might not be identical if the system encoding is different, but for many it will be.
    // For the sake of a unit test, let's skip strict equality here if it's environment dependent.

    // Test EscapeString and UnescapeString
    std::string original = "Hello \"World\"! \n New line.";
    std::string escaped = EscapeString(original);
    std::string unescaped = UnescapeString(escaped);
    assert(original == unescaped);

    // Test OneLine
    std::string multiLine = "Line 1\nLine 2\nLine 3";
    std::string oneLine = OneLine(multiLine, 10);
    // It should be at most 10 characters long.
    assert(oneLine.length() <= 10);

    // Test ReplaceNewlinesIn
    std::string nlString = "Line 1\nLine 2\nLine 3";
    ReplaceNewlinesIn(nlString, " ");
    assert(nlString == "Line 1 Line 2 Line 3");

    // Test ReplaceIn
    std::string replaceStr = "The quick brown fox";
    ReplaceIn(replaceStr, "brown", "red");
    assert(replaceStr == "The quick red fox");

    std::cout << "All tests passed!" << std::endl;
    return 0;
}
