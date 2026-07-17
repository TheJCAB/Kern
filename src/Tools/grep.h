#pragma once

#include <FileUtilities.h>
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
    auto                  const pattern     = arguments.at("pattern"     ).get_ref<std::string const&>();
    std::filesystem::path const rootDir     = arguments.at("root_dir"    ).get_ref<std::string const&>();
    std::string           const globPattern = arguments.at("glob_pattern").get_ref<std::string const&>();

    json response = {
        { "pattern"     , pattern     },
        { "root_dir"    , rootDir     },
        { "glob_pattern", globPattern },
    };

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
                    lineArray.push_back(json{
                        { "line", lineNumber },
                        { "text", line },
                    });
                }
            }
            fileArray.push_back(json{
                { "file" , globResult.name },
                { "lines", std::move(lineArray) },
            });
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
    StringToolParameter{ "glob_pattern", "The glob pattern of the files to search in, relative to root_dir. Supports '*', '**' and '?'" },
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
