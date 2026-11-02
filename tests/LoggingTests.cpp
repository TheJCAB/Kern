#include "Logging.h"

#include "TestUtilities.h"

#include <print>

int main()
{
    std::println("Test ParseLogLevel");
    Expect(ParseLogLevel("debug") == LogLevel::debug, "debug");
    Expect(ParseLogLevel("off") == LogLevel::off, "off");
    Expect(!ParseLogLevel("verbose").has_value(), "unknown level");

    std::println("Test IsLogEnabled");
    SetLogLevel(LogLevel::info);
    Expect(IsLogEnabled(LogLevel::error), "error is enabled at info");
    Expect(IsLogEnabled(LogLevel::info), "info is enabled at info");
    Expect(!IsLogEnabled(LogLevel::debug), "debug is disabled at info");
    SetLogLevel(LogLevel::off);
    Expect(!IsLogEnabled(LogLevel::error), "nothing is enabled when off");

    std::println("All tests passed!");
    return 0;
}
