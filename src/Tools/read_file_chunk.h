#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

constexpr int64_t MaxLines = 100;

inline json ReadFileChunkTool(json const& arguments, ToolsRuntimeContext const& context)
{
    std::filesystem::path const path      = arguments.value("path", "");
    int64_t               const startLine = arguments.value("start_line", int64_t{1});
    int64_t               const endLine   = arguments.value("end_line"  , INT64_MAX);

    json response
    {
        { "path"      , path      },
        { "start_line", startLine },
        { "end_line"  , endLine   },
    };

    if (startLine < 1)
    {
        response["error"] = "start_line must be greater than 1";
        return response;
    }

    if (endLine < 1)
    {
        response["error"] = "end_line must be greater than 1";
        return response;
    }    

    if (endLine < startLine)
    {
        response["error"] = "end_line must be equal or greater than start_line";
        return response;
    }

    try
    {
        auto const cappedEndLine = std::min(INT64_MAX - MaxLines + 1, startLine - 1) + MaxLines - 1;
        
        FileChunk chunk = context.fs.ReadTextFileChunk(path, startLine, cappedEndLine);

        response["truncated"] = !chunk.eof && static_cast<int64_t>(chunk.lines.size()) < endLine - startLine + 1;

        json::array_t lines;

        auto lineNumber = chunk.startLine;
        for (auto& line : chunk.lines)
        {
            lines.push_back(json{
                { "line", lineNumber },
                { "text", line       },
            });
            ++lineNumber;
        }

        response["lines"] = std::move(lines);
    }
    catch(const std::exception& e)
    {
        response["error"] = e.what();
    }

    return response;
}

constexpr ToolParameter ReadFileChunkToolRequiredParameters[] =
{
    StringToolParameter { "path", "The path to the file to read" },
};

constexpr ToolParameter ReadFileChunkToolOptionalParameters[] =
{
    IntegerToolParameter{ { "start_line", "The first line to include" }, std::optional<std::int64_t>{1}, std::nullopt },
    IntegerToolParameter{ { "end_line"  , "The last line to include"  }, std::optional<std::int64_t>{1}, std::nullopt },
};

constexpr ToolDefinition read_file_chunk
{
    .name               = "read_file_chunk",
    .description        = "Read any number of lines from a file between start_line and end_line. "
                          "It is highly recommended to use a range of lines to avoids loading too-large files. "
                          "The result may provide less lines than requested even if available, in which case it'll indicate it was \"truncated\"",
    .requiredParameters = ReadFileChunkToolRequiredParameters,
    .optionalParameters = ReadFileChunkToolOptionalParameters,
    .callTool           = ReadFileChunkTool,
};
