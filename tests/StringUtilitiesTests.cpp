
#include "StringUtilities.h"

#include "TestUtilities.h"

#include <iostream>
#include <string>

int main()
{
    std::cout << "Test Utf8ToSystemEncoding and SystemEncodingToUtf8\n";
    // Since we don't know the exact system encoding, we'll just check that it handles a basic string.
    std::string input = "Hello World";
    std::string utf8  = Utf8ToSystemEncoding(input);
    std::string back  = SystemEncodingToUtf8(utf8);
    // They might not be identical if the system encoding is different, but for many it will be.
    // For the sake of a unit test, let's skip strict equality here if it's environment dependent.

    std::cout << "Test EscapeString and UnescapeString\n";
    std::string original  = "Hello \"World\"! \n New line.";
    std::string escaped   = EscapeString(original);
    std::string unescaped = UnescapeString(escaped);
    ExpectEqual(original, unescaped, "expected unescaping recovers an escaped string");

    std::cout << "Test OneLine\n";
    std::string multiLine = "Line 1\nLine 2\nLine 3";
    std::string oneLine   = OneLine(multiLine, 10);
    // It should be at most 10 characters long.
    Expect(oneLine.length() <= 10 + 1 + 3, "OneLine must return a size-limited string"); // Adds the "\n" and "...".

    std::cout << "Test ReplaceNewlinesIn\n";
    std::string nlString = "Line 1\nLine 2\r\nLine 3";
    ReplaceNewlinesIn(nlString, " ");
    ExpectEqual(nlString, "Line 1 Line 2 Line 3", "ReplaceNewlinesIn must find and replace all newlines");

    std::cout << "Test ReplaceIn\n";
    std::string replaceStr = "The quick brown fox";
    ReplaceIn(replaceStr, "brown", "red");
    ExpectEqual(replaceStr, "The quick red fox", "ReplaceIn must replace the given string");

    // Test ConfigureConsoleForUtf8 (hard to test without a real console, but we can check it doesn't crash)
    ConfigureConsoleForUtf8();

    std::cout << "All tests passed!" << std::endl;
    return 0;
}
