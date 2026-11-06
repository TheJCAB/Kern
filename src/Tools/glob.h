#pragma once

#include <FileUtilities.h>
#include <JsonUtilities.h>
#include <ToolUtilities.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

constexpr std::size_t GlobMaxResults = 50;

inline json GlobTool(json const& arguments, ToolsRuntimeContext const& context)
{
    std::string const pattern = GetOrDefault(arguments, "pattern" , std::string{});
    std::string const rootDir = GetOrDefault(arguments, "root_dir", std::string{ "." });

    // Every response has the same shape and key order, whatever the outcome: a caller that reads
    // the envelope never has to guess whether a missing payload means failure or emptiness.
    json response
    {
        { "ok"       , true           },
        { "status"   , "ok"           },
        { "errorCode", nullptr        },
        { "summary"  , ""             },
        { "root_dir" , rootDir        },
        { "pattern"  , pattern        },
        { "ignored"  , json::array()  },
        { "exclude"  , json::array()  },
        { "returned" , 0              },
        { "total"    , 0              },
        { "truncated", false          },
        { "histogram", json::object() },
        { "matches"  , json::array()  },
    };

    auto const fail = [&](std::string const& errorCode, std::string const& summary)
    {
        response["ok"]        = false;
        response["status"]    = "error";
        response["errorCode"] = errorCode;
        response["summary"]   = summary;
        response["returned"]  = 0;
        response["truncated"] = false;
        response["histogram"] = json::object();
        response["matches"]   = json::array();
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

        std::filesystem::path const searchRoot = context.fs.ValidatePath(std::filesystem::path{ rootDir });
        if (!std::filesystem::exists(searchRoot) || !std::filesystem::is_directory(searchRoot))
        {
            fail("root_not_found", "parameter 'root_dir' is not an existing directory: " + searchRoot.generic_string());
            return response;
        }
        response["root_dir"] = searchRoot.generic_string();

        GlobOptions options;
        options.ignoredDirectories = context.ignoredDirectories;
        options.ignoredDirectories.insert(options.ignoredDirectories.end(), exclude.begin(), exclude.end());
        options.maxResults = GlobMaxResults;

        GlobMatches const globMatches = Glob(searchRoot, pattern, options);

        std::size_t const returned  = globMatches.matches.size();
        std::size_t const total     = globMatches.totalMatches;
        bool        const truncated = returned < total;

        response["ignored"]   = globMatches.ignoredDirectories;
        response["returned"]  = returned;
        response["total"]     = total;
        response["truncated"] = truncated;

        // The histogram counts everything found before capping, so it says where the remainder is.
        json histogram = json::object();
        for (auto const& [segment, count] : globMatches.countsByTopLevelDirectory)
        {
            histogram[segment] = count;
        }
        response["histogram"] = std::move(histogram);

        json::array_t matchArray;
        matchArray.reserve(returned);
        for (GlobResult const& match : globMatches.matches)
        {
            matchArray.push_back({
                { "path"  , match.name.generic_string()                         },
                { "is_dir", match.type == std::filesystem::file_type::directory },
            });
        }
        response["matches"] = std::move(matchArray);

        std::string summary = std::format("{} of {} matches in {} directories", returned, total, globMatches.countsByTopLevelDirectory.size());
        if (truncated)
        {
            summary += std::format(", capped at {}", GlobMaxResults);
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
        fail("unsupported_pattern", e.what());
    }
    catch (GlobPatternError const& e)
    {
        fail("invalid_pattern", e.what());
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

constexpr ToolParameter GlobToolParameters[] =
{
    StringToolParameter{ "pattern", "The glob pattern of the files to match, relative to root_dir. "
                                    "Supports '*', '?' within one path segment and '**' across segments. "
                                    "Braces and character classes are not supported. "
                                    "A leading '/' is accepted and means the same as writing the pattern without it." },
};

constexpr ToolParameter GlobToolOptionalParameters[] =
{
    StringToolParameter     { "root_dir", "The directory to match from. Defaults to '.', the process working directory." },
    StringArrayToolParameter{ "exclude" , "Extra directory names to skip, added to the default ignore set. "
                                          "Each entry is a bare directory name, not a path. Reported back in 'ignored'." },
};

constexpr ToolDefinition glob
{
    .name               = "glob",
    .description        = "Find files matching a glob pattern under a directory. '*' and '?' match within one path segment, "
                          "'**' matches any number of segments; braces and character classes are not supported. "
                          "The pattern is matched from root_dir directly, so write '**/x' to search the whole tree. "
                          "A leading '/' is accepted and changes nothing. root_dir defaults to '.', the process working "
                          "directory, which is not necessarily the workspace root. The build directory and .git are ignored "
                          "by default and listed in every result. Entries in exclude are bare directory names ignored in "
                          "addition and listed in ignored. At most 50 matches are returned; the total, a per-directory "
                          "histogram and a truncated flag always say what was left out.",
    .requiredParameters = GlobToolParameters,
    .optionalParameters = GlobToolOptionalParameters,
    .callTool           = GlobTool,
};
