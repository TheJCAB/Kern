#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

inline std::string ReadFileChunkTool(json const& arguments, ToolsRuntimeContext const& context)
{
    auto const& path       = arguments.at("path").get_ref<std::string const&>();
    auto const start_line  = arguments.value("start_line", int64_t{1});
    auto const end_line    = arguments.value("end_line", std::numeric_limits<int64_t>::max());

    FileChunk chunk = context.fs.ReadTextFileChunk(path, start_line, end_line);

    json response
    {
        { "path"      , path       },
        { "start_line", start_line },
        { "end_line"  , end_line   },
        { "truncated" , static_cast<int64_t>(chunk.lines.size()) < end_line - start_line + 1 },
    };

    auto& lines = response["lines"] = json::array();

    auto lineNumber = chunk.startLine;
    for (auto& line : chunk.lines)
    {
        lines.push_back(json{
            { "line", lineNumber },
            { "text", line },
        });
        ++lineNumber;
    }

    return response.dump();
}

constexpr ToolParameter ReadFileChunkToolParameters[] =
{
    StringToolParameter { { "path", "The path to the file to read"    } },
    IntegerToolParameter{ { "start_line", "The first line to include" }, std::optional<std::int64_t>{1}, std::nullopt },
    IntegerToolParameter{ { "end_line", "The last line to include"    }, std::optional<std::int64_t>{1}, std::nullopt },
};

constexpr ToolDefinition read_file_chunk
{
    .name               = "read_file_chunk",
    .description        = "Read up to 100 lines from a file between start_line and end_line. Useful for large files; avoids loading the whole file.",
    .requiredParameters = ReadFileChunkToolParameters,
    .optionalParameters = {},
    .callTool           = ReadFileChunkTool,
};
