#pragma once

#include <filesystem>
#include <string_view>
#include <source_location>

std::filesystem::path CreateTempDirectory(std::string_view const prefix);

void Expect(bool condition, std::string_view message, std::source_location const& = std::source_location::current());

template <typename T, typename U>
void ExpectEqual(T const& actual, U const& expected, std::string_view message, std::source_location const& location = std::source_location::current()) noexcept(false)
{
    if (actual != expected) Expect(false, message, location);
}
