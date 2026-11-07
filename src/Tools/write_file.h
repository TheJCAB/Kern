#pragma once

#include <StringUtilities.h>
#include <FileUtilities.h>
#include <ToolUtilities.h>

#include <filesystem>
#include <string_view>

inline json WriteFileTool(json const& arguments, ToolsRuntimeContext const& context)
{
    auto const path    = arguments.value("path"   , "");
    auto const content = arguments.value("content", "");

    try
    {
        std::filesystem::path const validated = context.fs.ValidateWritablePath(path);
        bool const created = !std::filesystem::exists(validated);
        context.fs.WriteTextFile(path, content);

        return json{
            { "path"       , path               },
            { "created"    , created            },
            { "lines_after", CountLines(content) },
            { "success"    , true               },
        };
    }
    catch(const std::exception& e)
    {
        return json{
            { "path"   , path     },
            { "success", false    },
            { "error"  , e.what() },
        };
    }
}

constexpr ToolParameter WriteFileToolParameters[] =
{
    StringToolParameter{ { "path"   , "The path to the file to write"    } },
    StringToolParameter{ { "content", "The content to write to the file. "
                                      "**Note: Provide the content as it should appear in the final file; do not double-escape backslashes or quotes" } },
};

constexpr ToolDefinition write_file
{
    .name               = "write_file",
    .description        = "Write text content to a file, replacing any existing contents. Returns whether the file was created and its new line count.",
    .requiredParameters = WriteFileToolParameters,
    .callTool           = WriteFileTool,
};
