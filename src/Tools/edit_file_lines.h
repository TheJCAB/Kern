#pragma once

#include <StringUtilities.h>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr int64_t ContextLinesPerSide = 3;
constexpr int64_t CurrentTextMaxLines = 6;

// Builds the [{ "line": n, "text": "..." }, ...] array of at most `maxLines` consecutive
// entries of `lines` starting at the 1-based line number `firstLine`. Entries that would fall
// past the end of `lines` are omitted, so a start past the end yields an empty array.
json BuildLineRange(std::vector<std::string> const& lines, int64_t const firstLine, int64_t const maxLines)
{
    json range = json::array();
    int64_t const lineCount = static_cast<int64_t>(lines.size());
    for (int64_t offset = 0; offset < maxLines; ++offset)
    {
        int64_t const lineNumber = firstLine + offset;
        if (lineNumber < 1 || lineNumber > lineCount)
        {
            break;
        }

        range.push_back(json{ { "line", lineNumber }, { "text", lines[static_cast<std::size_t>(lineNumber - 1)] } });
    }

    return range;
}
}

inline json EditFileLinesTool(json const& arguments, ToolsRuntimeContext const& context)
{
    std::filesystem::path const path      = arguments.value("path"      , "");
    std::string           const operation = arguments.value("operation" , "");
    std::int64_t          const startLine = arguments.value("start_line", 1 );
    std::int64_t          const endLine   = arguments.value("end_line"  , startLine);
    std::string           const content   = arguments.value("content"   , "");

    std::optional<std::string> expect{};
    if (arguments.contains("expect") && !arguments.at("expect").is_null())
    {
        expect = arguments.at("expect").get<std::string>();
    }

    if (startLine < 1)
    {
        return json{ { "path", path }, { "success", false }, { "error", "start_line must be at least 1" } };
    }

    if (endLine < 1)
    {
        return json{ { "path", path }, { "success", false }, { "error", "end_line must be at least 1" } };
    }

    if (endLine < startLine)
    {
        return json{ { "path", path }, { "success", false }, { "error", "end_line must be equal or greater than start_line" } };
    }

    std::optional<std::int64_t> linesBefore{};
    bool                        indicesResolved = false;

    try
    {
        std::string const original = context.fs.ReadTextFile(path);
        auto lines = SplitLines(original);
        linesBefore = static_cast<std::int64_t>(lines.size());

        if (operation != "insert" && operation != "replace" && operation != "delete")
        {
            throw std::runtime_error("error: unsupported operation");
        }

        auto const makeIndex = [&](int64_t const lineNumber, bool const allowPastEnd) -> std::size_t
        {
            std::size_t const maxLine = lines.size() + (allowPastEnd ? 1 : 0);
            if (lineNumber > static_cast<int64_t>(maxLine))
            {
                throw std::runtime_error(std::format("line number {} is too large beyond the end of the file", lineNumber));
            }

            return static_cast<std::size_t>(lineNumber - 1);
        };

        std::size_t const startIndex = makeIndex(startLine, operation == "insert");
        std::size_t       endIndex   = startIndex;
        if (operation != "insert")
        {
            endIndex = makeIndex(endLine, false);
            if (startIndex > endIndex)
            {
                throw std::runtime_error("error: start_line cannot be greater than end_line");
            }
        }
        indicesResolved = true;

        if (expect.has_value())
        {
            std::vector<std::string> const expectedLines = SplitLines(*expect);
            std::int64_t const regionCount = (operation == "insert") ? 0 : static_cast<std::int64_t>(endIndex - startIndex + 1);
            std::int64_t const expectedCount = static_cast<std::int64_t>(expectedLines.size());
            std::int64_t const comparisonCount = std::max(expectedCount, regionCount);

            std::int64_t firstDifferenceOffset = comparisonCount;
            for (std::int64_t offset = 0; offset < comparisonCount; ++offset)
            {
                bool const expectedPresent = offset < expectedCount;
                bool const regionPresent   = offset < regionCount;
                if (!expectedPresent || !regionPresent)
                {
                    firstDifferenceOffset = offset;
                    break;
                }

                std::string const& expectedLine = expectedLines[static_cast<std::size_t>(offset)];
                std::string const& regionLine   = lines[startIndex + static_cast<std::size_t>(offset)];
                if (expectedLine != regionLine)
                {
                    firstDifferenceOffset = offset;
                    break;
                }
            }

            if (firstDifferenceOffset < comparisonCount)
            {
                std::int64_t const firstDifferenceLine = startLine + firstDifferenceOffset;
                json currentText = BuildLineRange(lines, firstDifferenceLine, CurrentTextMaxLines);
                bool const currentTextTruncated = *linesBefore - firstDifferenceLine + 1 > CurrentTextMaxLines;

                return json{
                    { "path"                  , path                                    },
                    { "applied_start"         , startLine                               },
                    { "removed_lines"         , 0                                       },
                    { "inserted_lines"        , 0                                       },
                    { "lines_before"          , *linesBefore                            },
                    { "success"               , false                                   },
                    { "error"                 , std::format("expect did not match: first difference at line {}; file unchanged", firstDifferenceLine) },
                    { "current_text"          , currentText                             },
                    { "current_text_truncated", currentTextTruncated                   },
                };
            }
        }

        std::vector<std::string> const replacementLines = SplitLines(content);

        std::vector<std::string> updatedLines;
        if (operation == "insert")
        {
            updatedLines.reserve(lines.size() + replacementLines.size());
            updatedLines.insert(updatedLines.end(), lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(startIndex));
            updatedLines.insert(updatedLines.end(), replacementLines.begin(), replacementLines.end());
            updatedLines.insert(updatedLines.end(), lines.begin() + static_cast<std::ptrdiff_t>(startIndex), lines.end());
        }
        else if (operation == "replace")
        {
            updatedLines.reserve(lines.size() - (endIndex - startIndex + 1) + replacementLines.size());
            updatedLines.insert(updatedLines.end(), lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(startIndex));
            updatedLines.insert(updatedLines.end(), replacementLines.begin(), replacementLines.end());
            updatedLines.insert(updatedLines.end(), lines.begin() + static_cast<std::ptrdiff_t>(endIndex) + 1, lines.end());
        }
        else
        {
            updatedLines.reserve(lines.size() - (endIndex - startIndex + 1));
            updatedLines.insert(updatedLines.end(), lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(startIndex));
            updatedLines.insert(updatedLines.end(), lines.begin() + static_cast<std::ptrdiff_t>(endIndex) + 1, lines.end());
        }

        std::int64_t const removedLines  = (operation == "insert") ? 0 : static_cast<std::int64_t>(endIndex - startIndex + 1);
        std::int64_t const insertedLines = (operation == "delete") ? 0 : static_cast<std::int64_t>(replacementLines.size());

        std::string contentToWrite = JoinLines(updatedLines);
        bool const newlineAdded = !contentToWrite.empty() && original.ends_with('\n') && !contentToWrite.ends_with('\n');
        if (newlineAdded)
        {
            contentToWrite += '\n';
        }

        context.fs.WriteTextFile(path, contentToWrite);

        std::int64_t const linesAfter = static_cast<std::int64_t>(updatedLines.size());
        std::int64_t const regionEnd   = startLine + insertedLines - 1;
        std::int64_t const beforeStart = std::max<int64_t>(1, startLine - ContextLinesPerSide);
        std::int64_t const beforeCount = startLine - beforeStart;
        std::int64_t const afterStart  = regionEnd + 1;
        std::int64_t const afterCount  = std::max<int64_t>(0, std::min<int64_t>(ContextLinesPerSide, linesAfter - regionEnd));

        json const contextBefore = BuildLineRange(updatedLines, beforeStart, beforeCount);
        json const contextAfter  = BuildLineRange(updatedLines, afterStart, afterCount);
        bool const contextTruncated = beforeCount < ContextLinesPerSide || afterCount < ContextLinesPerSide;

        return json{
            { "path"               , path             },
            { "applied_start"      , startLine        },
            { "removed_lines"      , removedLines     },
            { "inserted_lines"     , insertedLines    },
            { "lines_before"       , *linesBefore     },
            { "lines_after"        , linesAfter       },
            { "newline_added"      , newlineAdded     },
            { "context_before"     , contextBefore    },
            { "context_after"      , contextAfter     },
            { "context_truncated"  , contextTruncated },
            { "success"            , true             },
        };
    }
    catch(const std::exception& e)
    {
        json response{
            { "path"   , path     },
            { "success", false    },
            { "error"  , e.what() },
        };

        if (linesBefore.has_value())
        {
            response["lines_before"] = *linesBefore;
        }

        if (indicesResolved)
        {
            response["applied_start"] = startLine;
        }

        return response;
    }
}

constexpr ToolParameter EditFileLinesToolParameters[] =
{
    StringToolParameter { { "path", "The path to the file to edit"                              } },
    StringToolParameter { { "operation", "The operation to perform: insert, replace, or delete" } },
    IntegerToolParameter{ { "start_line", "The first line to affect"                            }, 1 },
};

constexpr ToolParameter EditFileLinesToolOptionalParameters[] =
{
    IntegerToolParameter{ { "end_line", "The last line to affect. Defaults to start_line. Ignored when operation is insert." }, 1 },
    StringToolParameter { { "content" , "The replacement content to insert or replace with. "
                                        "**Note: Provide the content as it should appear in the final file; do not double-escape backslashes or quotes" } },
    StringToolParameter { { "expect"  , "The exact text you believe is currently at start_line..end_line. If it does not match, nothing is written and the current text there is returned." } },
};

constexpr ToolDefinition edit_file_lines
{
    .name               = "edit_file_lines",
    .description        = "Modify a file by inserting, replacing, or deleting lines. Returns the lines surrounding the edit after it is applied, and the file's new line count. Optionally pass expect: the exact text you believe is currently at start_line..end_line. If it does not match, nothing is written and the current text there is returned. end_line defaults to start_line; insert ignores end_line.",
    .requiredParameters = EditFileLinesToolParameters,
    .optionalParameters = EditFileLinesToolOptionalParameters,
    .callTool           = EditFileLinesTool,
};
