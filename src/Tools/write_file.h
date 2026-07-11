#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>

inline std::string WriteFileTool(json const& arguments, ToolsRuntimeContext const& context)
{
    auto& path    = arguments.at("path"   ).get_ref<std::string const&>();
    auto& content = arguments.at("content").get_ref<std::string const&>();

    context.fs.WriteTextFile(path, content);
    return "[write_file] ok";
}

constexpr ToolParameter WriteFileToolParameters[] =
{
    StringToolParameter{ { "path"   , "The path to the file to write"    } },
    StringToolParameter{ { "content", "The content to write to the file" } },
};

constexpr ToolDefinition write_file
{
    .name               = "write_file",
    .description        = "Write content to a file",
    .requiredParameters = WriteFileToolParameters,
    .callTool           = WriteFileTool,
};
