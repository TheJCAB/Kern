#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>

inline json WriteFileTool(json const& arguments, ToolsRuntimeContext const& context) noexcept
{
    auto const path    = arguments.value("path"   , "");
    auto const content = arguments.value("content", "");

    json response{
        { "path", path },
    };

    try
    {
        context.fs.WriteTextFile(path, content);
        response["success"] = true;
    }
    catch(const std::exception& e)
    {
        response["error"] = e.what();
    }

    return response;
}

constexpr ToolParameter WriteFileToolParameters[] =
{
    StringToolParameter{ { "path"   , "The path to the file to write"    } },
    StringToolParameter{ { "content", "The content to write to the file" } },
};

constexpr ToolDefinition write_file
{
    .name               = "write_file",
    .description        = "Write text content to a file",
    .requiredParameters = WriteFileToolParameters,
    .callTool           = WriteFileTool,
};
