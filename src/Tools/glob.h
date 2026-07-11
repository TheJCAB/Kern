#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

inline std::string GlobTool(json const& arguments, ToolsRuntimeContext const& context)
{
    std::filesystem::path const pattern = arguments.at("pattern").get_ref<std::string const&>();
    std::filesystem::path const rootDir = arguments.value("root_dir", ".");

    //std::filesystem::path const normalizedPattern = pattern.lexically_normal().lexically_proximate(rootDir.lexically_normal());
    //if (normalizedPattern.empty() || *normalizedPattern.begin() == "..")
    //{
    //    throw std::runtime_error("error: malformed pattern");
    //}

    std::filesystem::path const searchRoot = context.fs.ValidatePath(rootDir);
    if (!std::filesystem::exists(searchRoot))
    {
        throw std::runtime_error("error: root directory does not exist");
    }

    auto const matches = Glob(searchRoot, pattern);

    json response
    {
        { "pattern", pattern },
        { "root_dir", rootDir },
        { "matches", json::array() },
    };

    auto& matchArray = response["matches"];
    for (auto const& match : matches)
    {
        matchArray.push_back(match.generic_string());
    }

    return response.dump();
}

constexpr ToolParameter GlobToolParameters[] =
{
    StringToolParameter{ "pattern", "The glob pattern to match, relative to root_dir" },
};

constexpr ToolParameter GlobToolOptionalParameters[] =
{
    StringToolParameter{ "root_dir", "The directory to search from" },
};

constexpr ToolDefinition glob
{
    .name               = "glob",
    .description        = "Find files matching a glob pattern under a directory. Supports *, **, and ?.",
    .requiredParameters = GlobToolParameters,
    .optionalParameters = GlobToolOptionalParameters,
    .callTool           = GlobTool,
};
