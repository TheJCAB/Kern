#pragma once

#include <filesystem>
#include <format>
#include <string_view>
#include <source_location>

std::filesystem::path CreateTempDirectory(std::string_view const prefix);

void Expect(bool condition, std::string_view message, std::source_location const& = std::source_location::current());

template <typename T, typename U>
    requires !std::formattable<T, char> && !std::formattable<U, char>
void ExpectEqual(T const& actual, U const& expected, std::string_view message, std::source_location const& location = std::source_location::current()) noexcept(false)
{
    if (actual != expected) Expect(false, message, location);
}

template <typename T, typename U>
void ExpectEqual(T const& actual, U const& expected, std::string_view message, std::source_location const& location = std::source_location::current()) noexcept(false)
{
    if (actual == expected) return;

    std::string fullMessage;
    if constexpr (std::formattable<T, char>)
    {
        fullMessage = std::format("{} != ", actual);
    }
    else
    {
        fullMessage = std::format("_ != ");
    }
    if constexpr (std::formattable<T, char>)
    {
        fullMessage += std::format("{}: ", expected);
    }
    else
    {
        fullMessage += std::format("_:");
    }
    fullMessage += message;
    Expect(false, fullMessage, location);
}
