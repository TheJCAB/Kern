#pragma once

#include <ToolUtilities.h>

#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
std::vector<std::string> SplitLines(std::string_view const content)
{
    std::vector<std::string> lines;
    if (content.empty())
    {
        return lines;
    }

    std::istringstream input{std::string(content)};
    std::string line;
    while (std::getline(input, line))
    {
        lines.push_back(std::move(line));
    }

    return lines;
}

std::string JoinLines(std::vector<std::string> const& lines)
{
    if (lines.empty())
    {
        return {};
    }

    std::ostringstream output;
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        if (index > 0)
        {
            output << '\n';
        }
        output << lines[index];
    }
    return output.str();
}

std::vector<std::string> ParseReplacementLines(std::string_view const content)
{
    auto const lines = SplitLines(content);
    if (content.empty())
    {
        return {};
    }

    if (!content.ends_with('\n'))
    {
        return lines;
    }

    return lines;
}
}

inline std::string EditFileLinesTool(json const& arguments, ToolsRuntimeContext const& context)
{
    auto const& path = arguments.at("path").get_ref<std::string const&>();
    auto const& operation = arguments.at("operation").get_ref<std::string const&>();
    auto const startLine = arguments.value("start_line", int64_t{1});
    auto const endLine = arguments.value("end_line", startLine);
    auto const content = arguments.value("content", std::string{});

    if (startLine < 1 || endLine < 1 || endLine < startLine)
    {
        throw std::runtime_error("error: invalid line range");
    }

    std::string const original = context.fs.ReadTextFile(path);
    auto lines = SplitLines(original);

    auto const makeIndex = [&](int64_t const lineNumber, bool const allowPastEnd) -> std::size_t
    {
        if (lineNumber < 1)
        {
            throw std::runtime_error("error: line numbers must be >= 1");
        }

        std::size_t const maxLine = lines.size() + (allowPastEnd ? 1 : 0);
        if (lineNumber > static_cast<int64_t>(maxLine))
        {
            throw std::runtime_error("error: line number is out of range");
        }

        return static_cast<std::size_t>(lineNumber - 1);
    };

    std::vector<std::string> replacementLines = ParseReplacementLines(content);

    std::vector<std::string> updatedLines;
    if (operation == "insert")
    {
        auto const insertIndex = makeIndex(startLine, true);
        updatedLines.reserve(lines.size() + replacementLines.size());
        updatedLines.insert(updatedLines.end(), lines.begin(), lines.begin() + insertIndex);
        updatedLines.insert(updatedLines.end(), replacementLines.begin(), replacementLines.end());
        updatedLines.insert(updatedLines.end(), lines.begin() + insertIndex, lines.end());
    }
    else if (operation == "replace")
    {
        auto const startIndex = makeIndex(startLine, false);
        auto const endIndex = makeIndex(endLine, false);
        if (startIndex > endIndex)
        {
            throw std::runtime_error("error: start_line cannot be greater than end_line");
        }

        updatedLines.reserve(lines.size() - (endIndex - startIndex + 1) + replacementLines.size());
        updatedLines.insert(updatedLines.end(), lines.begin(), lines.begin() + startIndex);
        updatedLines.insert(updatedLines.end(), replacementLines.begin(), replacementLines.end());
        updatedLines.insert(updatedLines.end(), lines.begin() + endIndex + 1, lines.end());
    }
    else if (operation == "delete")
    {
        auto const startIndex = makeIndex(startLine, false);
        auto const endIndex = makeIndex(endLine, false);
        if (startIndex > endIndex)
        {
            throw std::runtime_error("error: start_line cannot be greater than end_line");
        }

        updatedLines.reserve(lines.size() - (endIndex - startIndex + 1));
        updatedLines.insert(updatedLines.end(), lines.begin(), lines.begin() + startIndex);
        updatedLines.insert(updatedLines.end(), lines.begin() + endIndex + 1, lines.end());
    }
    else
    {
        throw std::runtime_error("error: unsupported operation");
    }

    RawWriteTextFile(path, JoinLines(updatedLines));

    json response
    {
        { "path", path },
        { "operation", operation },
        { "start_line", startLine },
        { "end_line", endLine },
        { "ok", true },
    };

    return response.dump();
}

constexpr ToolParameter EditFileLinesToolParameters[] =
{
    StringToolParameter { { "path", "The path to the file to edit"                              } },
    StringToolParameter { { "operation", "The operation to perform: insert, replace, or delete" } },
    IntegerToolParameter{ { "start_line", "The first line to affect"                            }, 1 },
};

constexpr ToolParameter EditFileLinesToolOptionalParameters[] =
{
    IntegerToolParameter{ { "end_line", "The last line to affect"                          }, 1 },
    StringToolParameter { { "content", "The replacement content to insert or replace with" } },
};

constexpr ToolDefinition edit_file_lines
{
    .name               = "edit_file_lines",
    .description        = "Modify a file by inserting, replacing, or deleting lines.",
    .requiredParameters = EditFileLinesToolParameters,
    .optionalParameters = EditFileLinesToolOptionalParameters,
    .callTool           = EditFileLinesTool,
};
