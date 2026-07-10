#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>

inline std::string ReadFileTool(json const& arguments)
{
    auto& path = arguments.at("path").get_ref<std::string const&>();
    return ReadTextFile(path);
}

constexpr ToolParameter ReadFileToolParameters[] =
{
    StringToolParameter{ "path", "The path to the file to read" },
};    

constexpr ToolDefinition read_file
{
    .name               = "read_file",
    .description        = "Read the contents of a file",
    .requiredParameters = ReadFileToolParameters,
    .callTool           = ReadFileTool,
};
