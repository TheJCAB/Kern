#pragma once

#include <JsonUtilities.h>
#include <ToolUtilities.h>

#include <filesystem>
#include <string_view>

inline json DeleteFileTool(json const& arguments, ToolsRuntimeContext const& context, std::string_view /* parentSessionId */)
{
    auto const path = arguments.value("path", "");

    try
    {
        std::filesystem::path const validated = context.fs.ValidateWritablePath(path);

        if (!std::filesystem::exists(validated))
        {
            return json{
                { "path"   , path                   },
                { "deleted", false                  },
                { "error"  , "no file at this path" },
            };
        }

        if (std::filesystem::is_directory(validated))
        {
            return json{
                { "path"   , path },
                { "deleted", false },
                { "error"  , "this removes one file only, never a directory or its contents" },
            };
        }

        bool const deleted = std::filesystem::remove(validated);

        return json{
            { "path"   , path    },
            { "deleted", deleted },
        };
    }
    catch(const std::exception& e)
    {
        return json{
            { "path"   , path     },
            { "deleted", false    },
            { "error"  , e.what() },
        };
    }
}

constexpr ToolParameter DeleteFileToolParameters[] =
{
    StringToolParameter{ "path", "The path of the file to delete" },
};

constexpr ToolDefinition delete_file
{
    .name               = "delete_file",
    .description        = "Delete exactly one file from the workspace. It refuses directories, unknown roots and protected paths, and there is no undo. Returns whether a file was actually removed.",
    .requiredParameters = DeleteFileToolParameters,
    .callTool           = DeleteFileTool,
    .promptFragment     = "Never remove a file the task did not ask you to remove. A deletion that turns out to be wrong "
                          "cannot be undone, so when in doubt leave the file in place and say why.",
};
