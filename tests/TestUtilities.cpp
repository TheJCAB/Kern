#include "TestUtilities.h"

import std;
//#include <chrono>
//#include <format>
//#include <iostream>
//#include <string>

std::filesystem::path CreateTempDirectory(std::string_view const prefix)
{
    auto const root        = std::filesystem::temp_directory_path();
    auto const uniqueCount = std::chrono::steady_clock::now().time_since_epoch().count();
    auto const unique      = root / (std::string(prefix) + std::to_string(uniqueCount));
    std::filesystem::create_directories(unique);
    return unique;
}

void Expect(bool condition, std::string_view message, std::source_location const& location) noexcept(false)
{
    if (!condition)
    {
        auto const fullMessage = std::format("{}:{},{}: error in function {} : {}", location.file_name(), location.line(), location.column(),  location.function_name(), message);
        std::cerr << fullMessage << std::endl;
        throw std::runtime_error{ fullMessage };
    }
}
