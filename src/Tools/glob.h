#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

inline json GlobTool(json const& arguments, ToolsRuntimeContext const& context)
{
    std::string           const pattern = arguments.value("pattern" , "");
    std::filesystem::path const rootDir = arguments.value("root_dir", ".");

    json response
    {
        { "pattern" , pattern },
        { "root_dir", rootDir },
    };

    try
    {
        std::filesystem::path const searchRoot = context.fs.ValidatePath(rootDir);
        if (!std::filesystem::exists(searchRoot))
        {
            response["error"] = "root_dir is not a valid root directory";
            return response;
        }    

        json::array_t matchArray;
        for (auto const& match : Glob(searchRoot, pattern))
        {
            matchArray.push_back({
                { "path"  , match.name.generic_string() },
                { "is_dir", match.type == std::filesystem::file_type::directory },
            });
        }

        response["matches"] = std::move(matchArray);
    }
    catch(const std::exception& e)
    {
        response["error"] = e.what();
    }

    return response;
}

constexpr ToolParameter GlobToolParameters[] =
{
    StringToolParameter{ "pattern", "The glob pattern to match, relative to root_dir. Supports '*', '**' and '?'" },
};

constexpr ToolParameter GlobToolOptionalParameters[] =
{
    StringToolParameter{ "root_dir", "The directory to search from, defaults to the workspace root" },
};

constexpr ToolDefinition glob
{
    .name               = "glob",
    .description        = "Find files matching a glob pattern under a directory.",
    .requiredParameters = GlobToolParameters,
    .optionalParameters = GlobToolOptionalParameters,
    .callTool           = GlobTool,
};
