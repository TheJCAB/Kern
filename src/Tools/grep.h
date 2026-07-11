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

// TODO: Search in files using glob wildcards.
// TODO: Search using regular expressions.
// TODO: Allow case-insensitive searches (UNICODE makes this "fun"!?).

std::string GrepTool(json const& arguments, ToolsRuntimeContext const& context)
{
    auto                  const pattern  = arguments.at("pattern").get_ref<std::string const&>();
    std::filesystem::path const filePath = arguments.at("path"   ).get_ref<std::string const&>();

    std::ifstream file(context.fs.ValidatePath(filePath));
    if (!file.is_open())
    {
        throw std::runtime_error("error: could not open file: " + filePath.string());
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
        { "pattern", pattern       },
        { "path"   , filePath      },
        { "matches", json::array() },
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
    StringToolParameter{ { "pattern", "The search string to look for in the file" } },
    StringToolParameter{ { "path"   , "The path to the file to search"            } },
};

constexpr ToolDefinition grep
{
    .name               = "grep",
    .description        = "Search for a pattern within a file. Supports basic string matching",
    .requiredParameters = GrepToolParameters,
    .callTool           = GrepTool,
};
