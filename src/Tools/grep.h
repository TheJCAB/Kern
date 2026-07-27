#pragma once

#include <FileUtilities.h>
#include <JsonUtilities.h>
#include <StringUtilities.h>
#include <ToolUtilities.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

json GrepTool(json const& arguments, ToolsRuntimeContext const& context)
{
    std::string           const pattern     = GetOrDefault(arguments, "pattern"     , "");
    std::filesystem::path const rootDir     = GetOrDefault(arguments, "root_dir"    , ".");
    std::string           const globPattern = GetOrDefault(arguments, "glob_pattern", "");

    json response = {
        { "pattern"     , pattern     },
        { "root_dir"    , rootDir     },
        { "glob_pattern", globPattern },
    };

    if (pattern.empty())
    {
        response["error"] = "The file pattern is missing";
        return response;
    }

    if (globPattern.empty())
    {
        response["error"] = "The glob pattern is missing";
        return response;
    }

    try
    {
        // TODO: Make case insensitive searches optional.
        // TODO: Study UTF-8 and UNICODE support in general.
        std::regex patternRegex(pattern, std::regex_constants::egrep | std::regex_constants::icase | std::regex_constants::nosubs);

        json::array_t fileArray;
        
        for (auto&& globResult : Glob(rootDir, globPattern))
        {
            if (globResult.type != std::filesystem::file_type::regular)
            {
                continue;
            }

            auto const filePath = rootDir / globResult.name;
            // TODO: Implement and use streaming reads via ReadTextFileChunk
            std::ifstream file(context.fs.ValidatePath(filePath));
            if (!file.is_open())
            {
                fileArray.push_back(json{
                    { "file" , globResult.name       },
                    { "error", "could not open file" },
                });
                continue;
            }

            json::array_t lineArray;

            std::string line;
            for (unsigned lineNumber = 1; lineNumber < UINT_MAX; ++lineNumber)
            {
                if (!std::getline(file, line))
                {
                    break;
                }
                if (std::regex_search(line, patternRegex))
                {
                    auto const truncatedLine = std::string_view{ line }.substr(0, std::min<size_t>(2048, line.size()));
                    lineArray.push_back(json{
                        { "line", lineNumber },
                        { "text", truncatedLine },
                    });
                    if (line.size() > truncatedLine.size())
                    {
                        lineArray.back()["line_truncated"] = true;
                    }
                }
            }
            if (!lineArray.empty())
            {
                fileArray.push_back(json{
                    { "file" , globResult.name },
                    { "lines", std::move(lineArray) },
                });
            }
        }

        response["matches"] = std::move(fileArray);
    }
    catch(const std::exception& e)
    {
        response["error"] = e.what();
    }

    return response;
}

} // namespace

constexpr ToolParameter GrepToolParameters[] =
{
    StringToolParameter{ "pattern"     , "The search string to look for in the file, using \"egrep\" format" },
    StringToolParameter{ "glob_pattern", "The glob pattern of the files to search in, relative to root_dir. "
                                         "Supports '*', '**' and '?'. "
                                         "Start with '/' to force a match just at the root, not insubdirectories." },
};

constexpr ToolParameter GrepToolOptionalParameters[] =
{
    StringToolParameter{ "root_dir", "The directory to search from, defaults to the workspace root" },
};

constexpr ToolDefinition grep
{
    .name               = "grep",
    .description        = "Search for lines containing a pattern within a file or set of files",
    .requiredParameters = GrepToolParameters,
    .callTool           = GrepTool,
};
