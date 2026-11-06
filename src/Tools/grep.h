#pragma once

#include <FileUtilities.h>
#include <JsonUtilities.h>
#include <ToolUtilities.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

constexpr std::size_t   GrepMaxFiles             = 20;
constexpr std::int64_t  GrepMaxLinesPerFileLimit = 50;
constexpr std::size_t   GrepMaxLineBytes         = 512;

namespace
{

// The histogram key of a relative name: its first segment below root_dir, or "." for a file that
// sits directly in root_dir.
std::string GrepHistogramKey(std::string const& relativeName)
{
    std::size_t const separator = relativeName.find('/');
    if (separator == std::string::npos)
    {
        return ".";
    }
    return relativeName.substr(0, separator);
}

// Cuts a line at GrepMaxLineBytes and backs off the trailing UTF-8 continuation bytes, so that
// the cut happens on a character boundary instead of inside a sequence.
std::string GrepTruncateLine(std::string_view line)
{
    std::string truncated{ line.substr(0, std::min(GrepMaxLineBytes, line.size())) };
    while (!truncated.empty() && (static_cast<unsigned char>(truncated.back()) & 0xC0) == 0x80)
    {
        truncated.pop_back();
    }
    return truncated;
}

json GrepTool(json const& arguments, ToolsRuntimeContext const& context)
{
    std::string  const pattern            = GetOrDefault(arguments, "pattern"            , std::string{});
    std::string  const globPattern        = GetOrDefault(arguments, "glob_pattern"       , std::string{});
    std::string  const rootDir            = GetOrDefault(arguments, "root_dir"           , std::string{ "." });
    bool         const caseSensitive      = GetOrDefault(arguments, "case_sensitive"     , false);
    bool         const filesWithMatches   = GetOrDefault(arguments, "files_with_matches" , false);
    std::int64_t const maxLinesArgument   = GetOrDefault(arguments, "max_lines_per_file" , std::int64_t{ GrepMaxLinesPerFileLimit });

    // Every response has the same shape and key order, whatever the outcome: a caller that reads
    // the envelope never has to guess whether a missing payload means failure or emptiness.
    json response
    {
        { "ok"                 , true           },
        { "status"             , "ok"           },
        { "errorCode"          , nullptr        },
        { "summary"            , ""             },
        { "root_dir"           , rootDir        },
        { "pattern"            , pattern        },
        { "glob_pattern"       , globPattern    },
        { "ignored"            , json::array()  },
        { "exclude"            , json::array()  },
        { "case_sensitive"     , caseSensitive  },
        { "files_with_matches" , filesWithMatches },
        { "max_lines_per_file" , maxLinesArgument },
        { "returned"           , 0              },
        { "total"              , 0              },
        { "truncated"          , false          },
        { "histogram"          , json::object() },
        { "matches"            , json::array()  },
    };

    auto const fail = [&](std::string const& errorCode, std::string const& summary)
    {
        response["ok"]                 = false;
        response["status"]             = "error";
        response["errorCode"]          = errorCode;
        response["summary"]            = summary;
        response["max_lines_per_file"] = nullptr;
        response["returned"]           = 0;
        response["total"]              = 0;
        response["truncated"]          = false;
        response["histogram"]          = json::object();
        response["matches"]            = json::array();
    };

    try
    {
        std::vector<std::string> const exclude = ParseExcludeArgument(arguments);
        response["exclude"] = exclude;

        if (pattern.empty())
        {
            fail("invalid_argument", "parameter 'pattern' is required and must not be empty");
            return response;
        }

        if (globPattern.empty())
        {
            fail("invalid_argument", "parameter 'glob_pattern' is required and must not be empty");
            return response;
        }

        // An out-of-range max_lines_per_file narrows nothing and is clamped rather than rejected,
        // because the cap can only ever be lowered and the caller still gets a usable answer.
        std::int64_t const maxLinesPerFile = std::clamp<std::int64_t>(maxLinesArgument, 1, GrepMaxLinesPerFileLimit);
        response["max_lines_per_file"] = maxLinesPerFile;

        std::filesystem::path const searchRoot = context.fs.ValidatePath(std::filesystem::path{ rootDir });
        if (!std::filesystem::exists(searchRoot) || !std::filesystem::is_directory(searchRoot))
        {
            fail("root_not_found", "parameter 'root_dir' is not an existing directory: " + searchRoot.generic_string());
            return response;
        }
        response["root_dir"] = searchRoot.generic_string();

        // egrep syntax, case-insensitive unless asked otherwise, and no subexpressions to capture
        // because only a yes/no answer per line is needed.
        std::regex::flag_type const regexFlags = std::regex_constants::egrep
                                               | (caseSensitive ? std::regex::flag_type{}
                                                                : std::regex_constants::icase)
                                               | std::regex_constants::nosubs;
        std::regex const regex{ pattern, regexFlags };

        // grep needs every candidate file, so the glob is uncapped: the file cap is applied below,
        // where the number of files that actually match is known.
        GlobOptions options;
        options.ignoredDirectories = context.ignoredDirectories;
        options.ignoredDirectories.insert(options.ignoredDirectories.end(), exclude.begin(), exclude.end());
        options.maxResults = 0;

        GlobMatches const globMatches = Glob(searchRoot, globPattern, options);
        response["ignored"] = globMatches.ignoredDirectories;

        json::array_t matchArray;
        json::array_t lineArray;

        std::map<std::string, std::size_t> histogram;

        std::size_t filesWithMatchesFound = 0;   // Files with a match seen before the file cap stops the scan.
        std::size_t returnedLines         = 0;   // Kept lines across all files.
        bool        stoppedAtFileCap      = false;
        bool        anyFileTruncated      = false;
        bool        anyLineTruncated      = false;

        // True when stopping here would leave a still-unvisited regular file behind, which is what
        // makes the file cap an honest truncation rather than a coincidence of the last match.
        auto const hasMoreRegularFiles = [&globMatches](std::size_t index)
        {
            for (++index; index < globMatches.matches.size(); ++index)
            {
                if (globMatches.matches[index].type == std::filesystem::file_type::regular)
                {
                    return true;
                }
            }
            return false;
        };

        for (std::size_t index = 0; index < globMatches.matches.size(); ++index)
        {
            GlobResult const& match = globMatches.matches[index];

            if (match.type != std::filesystem::file_type::regular)
            {
                continue;
            }

            // A file whose resolved path escapes the search root is reported in place rather than
            // aborting the whole search, exactly like an unreadable file.
            std::filesystem::path filePath;
            try
            {
                filePath = context.fs.ValidatePath(searchRoot / match.name);
            }
            catch (std::exception const& e)
            {
                matchArray.push_back(json{
                    { "file"     , match.name.generic_string() },
                    { "returned" , 0                           },
                    { "truncated", false                       },
                    { "lines"    , json::array()               },
                    { "error"    , e.what()                    },
                });
                if (matchArray.size() >= GrepMaxFiles)
                {
                    stoppedAtFileCap = hasMoreRegularFiles(index);
                    break;
                }
                continue;
            }

            // TODO: Implement and use streaming reads via ReadTextFileChunk
            std::ifstream file{ filePath };
            if (!file.is_open())
            {
                // One unreadable file must not abort the search: it is reported in place.
                matchArray.push_back(json{
                    { "file"     , match.name.generic_string() },
                    { "returned" , 0                           },
                    { "truncated", false                       },
                    { "lines"    , json::array()               },
                    { "error"    , "could not open file"       },
                });
                if (matchArray.size() >= GrepMaxFiles)
                {
                    stoppedAtFileCap = hasMoreRegularFiles(index);
                    break;
                }
                continue;
            }

            lineArray.clear();

            std::size_t   fileTotal  = 0;
            std::size_t   lineNumber = 0;
            std::string   line;
            while (std::getline(file, line))
            {
                ++lineNumber;
                if (!std::regex_search(line, regex))
                {
                    continue;
                }

                ++fileTotal;

                // In files_with_matches mode every line is still counted, but no lines are kept.
                if (filesWithMatches || lineArray.size() >= static_cast<std::size_t>(maxLinesPerFile))
                {
                    continue;
                }

                bool const lineTruncated = line.size() > GrepMaxLineBytes;
                json lineJson{
                    { "line", lineNumber                 },
                    { "text", GrepTruncateLine(line)     },
                };
                if (lineTruncated)
                {
                    lineJson["line_truncated"] = true;
                    anyLineTruncated = true;
                }
                lineArray.push_back(std::move(lineJson));
            }

            if (fileTotal == 0)
            {
                continue;
            }

            ++filesWithMatchesFound;
            ++histogram[GrepHistogramKey(match.name.generic_string())];

            if (filesWithMatches)
            {
                matchArray.push_back(json{
                    { "file"     , match.name.generic_string() },
                    { "total"    , fileTotal                   },
                    { "returned" , fileTotal                   },
                });
            }
            else
            {
                bool const fileTruncated = fileTotal > lineArray.size();
                anyFileTruncated = anyFileTruncated || fileTruncated;
                returnedLines += lineArray.size();
                matchArray.push_back(json{
                    { "file"     , match.name.generic_string() },
                    { "returned" , lineArray.size()            },
                    { "truncated", fileTruncated               },
                    { "lines"    , std::move(lineArray)        },
                });
                lineArray = json::array_t{};
            }

            if (matchArray.size() >= GrepMaxFiles)
            {
                stoppedAtFileCap = hasMoreRegularFiles(index);
                break;
            }
        }

        std::size_t const returnedFiles = matchArray.size();
        std::size_t const returned      = filesWithMatches ? returnedFiles : returnedLines;
        bool        const truncated     = stoppedAtFileCap
                                       || anyFileTruncated
                                       || (!filesWithMatches && anyLineTruncated);

        response["returned"]           = returned;
        response["total"]              = filesWithMatchesFound;
        response["truncated"]          = truncated;
        response["max_lines_per_file"] = filesWithMatches ? json{ nullptr } : json{ maxLinesPerFile };

        // Largest first, and stable by key for ties because the map iterates in key order.
        std::vector<std::pair<std::string, std::size_t>> histogramEntries{ histogram.begin(), histogram.end() };
        std::stable_sort(histogramEntries.begin(), histogramEntries.end(),
            [](auto const& left, auto const& right) { return left.second > right.second; });

        json histogramJson = json::object();
        for (auto const& [segment, count] : histogramEntries)
        {
            histogramJson[segment] = count;
        }
        response["histogram"] = std::move(histogramJson);
        response["matches"]   = std::move(matchArray);

        std::string summary;
        if (filesWithMatches)
        {
            summary = std::format("{} of {} files with matches", returned, filesWithMatchesFound);
        }
        else
        {
            summary = std::format("{} of {} files shown, {} lines, up to {} per file and {} bytes per line",
                returnedFiles, filesWithMatchesFound, returned, maxLinesPerFile, GrepMaxLineBytes);
        }
        if (stoppedAtFileCap)
        {
            summary += std::format(", capped at {} files", GrepMaxFiles);
        }
        if (!globMatches.ignoredDirectories.empty())
        {
            summary += ", ignored ";
            for (std::size_t index = 0; index < globMatches.ignoredDirectories.size(); ++index)
            {
                if (index != 0)
                {
                    summary += ", ";
                }
                summary += globMatches.ignoredDirectories[index];
            }
        }
        response["summary"] = std::move(summary);
    }
    catch (std::filesystem::filesystem_error const& e)
    {
        std::string summary = e.what();
        if (!e.path1().empty())
        {
            summary += " (path: " + e.path1().generic_string() + ")";
        }
        fail("io_error", summary);
    }
    catch (UnsupportedGlobPatternError const& e)
    {
        fail("unsupported_pattern", "parameter 'glob_pattern': " + std::string{ e.what() });
    }
    catch (GlobPatternError const& e)
    {
        fail("invalid_pattern", "parameter 'glob_pattern': " + std::string{ e.what() });
    }
    catch (std::regex_error const& e)
    {
        fail("invalid_pattern", "parameter 'pattern': " + std::string{ e.what() });
    }
    catch (ToolArgumentError const& e)
    {
        fail("invalid_argument", e.what());
    }
    catch (std::exception const& e)
    {
        fail("exception", e.what());
    }

    return response;
}

} // namespace

constexpr ToolParameter GrepToolParameters[] =
{
    StringToolParameter{ "pattern"     , "The regular expression to search for, in egrep format." },
    StringToolParameter{ "glob_pattern", "The glob pattern of the files to search, relative to root_dir. "
                                         "Supports '*', '?' within one path segment and '**' across segments. "
                                         "Braces and character classes are not supported." },
};

constexpr ToolParameter GrepToolOptionalParameters[] =
{
    StringToolParameter     { "root_dir", "The directory to search from. Defaults to '.', the process working directory." },
    StringArrayToolParameter{ "exclude" , "Extra directory names to skip, added to the default ignore set. "
                                          "Each entry is a bare directory name, not a path. Reported back in 'ignored'." },
    BoolToolParameter       { "case_sensitive", "Match case sensitively. Defaults to false: matching is case-insensitive." },
    BoolToolParameter       { "files_with_matches", "Return only the files that contain at least one match, with a per-file "
                                                    "match count and no matching lines. Defaults to false." },
    IntegerToolParameter    { { "max_lines_per_file", "Maximum matching lines to return per file. Defaults to 50 and is "
                                                      "clamped to 1-50; use 1 for one representative line per file." },
                              std::optional<std::int64_t>{ 1 }, std::optional<std::int64_t>{ 50 } },
};

constexpr ToolDefinition grep
{
    .name               = "grep",
    .description        = "Search for lines matching a regular expression (egrep format) in the files matched by "
                          "glob_pattern. Matching is case-insensitive unless case_sensitive is true. Set "
                          "files_with_matches to get only the files that contain matches, each with its match count "
                          "and no lines; max_lines_per_file (1-50, default 50) reduces the number of matching lines "
                          "returned per file. root_dir defaults to '.', the process working directory. The build "
                          "directory and .git are ignored by default and listed in every result. At most 20 files and "
                          "50 matching lines per file are returned, each line capped at 512 bytes; the totals, a "
                          "per-directory histogram and a truncated flag always say what was left out.",
    .requiredParameters = GrepToolParameters,
    .optionalParameters = GrepToolOptionalParameters,
    .callTool           = GrepTool,
};
