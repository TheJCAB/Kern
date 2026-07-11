#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

// Placeholder for search logic (similar to GlobTool but for grep)
std::string GrepTool(json const& arguments)
{
    // For demonstration, we will assume the tool searches a single file for a pattern.
    auto const pattern  = arguments.at("pattern"  ).get_ref<std::string const&>();
    auto const filePath = arguments.at("file_path").get_ref<std::string const&>();

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        throw std::runtime_error("error: could not open file: " + filePath);
    }

    std::string              line;
    std::vector<std::string> matches;

    while (std::getline(file, line))
    {
        if (line.find(pattern) != std::string::npos)
        {
            matches.push_back(line);
        }
    }

    // Construct JSON response
    json response = {
        {  "pattern",       pattern},
        {"file_path",      filePath},
        {  "matches", json::array()},
    };

    auto& matchArray = response["matches"];
    for (auto const& match : matches)
    {
        matchArray.push_back(match);
    }

    return response.dump();
}

} // namespace

constexpr ToolParameter GrepToolParameters[] =
{
    StringToolParameter{ { "pattern"  , "The search pattern to look for in the file." } },
    StringToolParameter{ { "file_path", "The path to the file to search."             } },
};

constexpr ToolDefinition grep
{
    .name               = "grep",
    .description        = "Search for a pattern within a file. Supports basic string matching.",
    .requiredParameters = GrepToolParameters,
    .callTool           = GrepTool,
};
