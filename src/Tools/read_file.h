#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>

inline json ReadFileTool(json const& arguments, ToolsRuntimeContext const& context)
{
    auto const path = arguments.value("path", "");

    json response{
        { "path", path },
    };
    
    try
    {
        response["contents"] = context.fs.ReadTextFile(path);
    }
    catch(const std::exception& e)
    {
        response["error"] = e.what();
    }

    return response;
}

constexpr ToolParameter ReadFileToolParameters[] =
{
    StringToolParameter{ { "path", "The path to the file to read" } },
};

constexpr ToolDefinition read_file
{
    .name               = "read_file",
    .description        = "Read the contents of a file",
    .requiredParameters = ReadFileToolParameters,
    .callTool           = ReadFileTool,
};
