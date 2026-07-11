#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
std::string NormalizeGlobPath(std::string_view const path)
{
    std::string normalized;
    normalized.reserve(path.size());
    for (char const ch : path)
    {
        normalized.push_back(ch == '\\' ? '/' : ch);
    }

    while (!normalized.empty() && normalized[0] == '/')
    {
        normalized.erase(normalized.begin());
    }

    while (!normalized.empty() && normalized.size() > 1 && normalized.rfind("./") == 0)
    {
        normalized.erase(0, 2);
    }

    return normalized;
}

std::string StripLeadingRootPrefix(std::string_view const pattern, std::string_view const root)
{
    std::string normalizedPattern = NormalizeGlobPath(pattern);
    std::string normalizedRoot = NormalizeGlobPath(root);

    if (normalizedRoot.empty() || normalizedRoot == ".")
    {
        return normalizedPattern;
    }

    if (normalizedPattern.empty())
    {
        return {};
    }

    if (normalizedPattern == normalizedRoot)
    {
        return {};
    }

    std::string const prefix = normalizedRoot + "/";
    if (normalizedPattern.starts_with(prefix))
    {
        return normalizedPattern.substr(prefix.size());
    }

    return normalizedPattern;
}

bool MatchGlobSegment(std::string_view const pattern, std::string_view const text)
{
    std::size_t patternIndex = 0;
    std::size_t textIndex = 0;
    std::size_t lastStar = std::string_view::npos;
    std::size_t lastMatch = 0;

    while (textIndex < text.size())
    {
        if (patternIndex < pattern.size() && pattern[patternIndex] == '*')
        {
            lastStar = patternIndex;
            lastMatch = textIndex;
            ++patternIndex;
        }
        else if (patternIndex < pattern.size() && (pattern[patternIndex] == '?' || pattern[patternIndex] == text[textIndex]))
        {
            ++patternIndex;
            ++textIndex;
        }
        else if (lastStar != std::string_view::npos)
        {
            patternIndex = lastStar + 1;
            ++lastMatch;
            textIndex = lastMatch;
        }
        else
        {
            return false;
        }
    }

    while (patternIndex < pattern.size() && pattern[patternIndex] == '*')
    {
        ++patternIndex;
    }

    return patternIndex == pattern.size();
}

bool MatchGlobPath(std::string_view const pattern, std::string_view const text)
{
    auto const patternSegments = [&]()
    {
        std::vector<std::string_view> segments;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= pattern.size(); ++i)
        {
            if (i == pattern.size() || pattern[i] == '/')
            {
                if (i > start)
                {
                    segments.emplace_back(pattern.data() + start, i - start);
                }
                else if (i > 0 && pattern[i - 1] == '/')
                {
                    segments.emplace_back("", 0);
                }
                start = i + 1;
            }
        }
        return segments;
    }();

    auto const textSegments = [&]()
    {
        std::vector<std::string_view> segments;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= text.size(); ++i)
        {
            if (i == text.size() || text[i] == '/')
            {
                if (i > start)
                {
                    segments.emplace_back(text.data() + start, i - start);
                }
                else if (i > 0 && text[i - 1] == '/')
                {
                    segments.emplace_back("", 0);
                }
                start = i + 1;
            }
        }
        return segments;
    }();

    std::function<bool(std::size_t, std::size_t)> matchSegments = [&](std::size_t patternIndex, std::size_t textIndex) -> bool
    {
        if (patternIndex == patternSegments.size())
        {
            return textIndex == textSegments.size();
        }

        auto const& patternSegment = patternSegments[patternIndex];
        if (patternSegment == "**")
        {
            if (patternIndex + 1 == patternSegments.size())
            {
                return true;
            }
            for (std::size_t i = textIndex; i <= textSegments.size(); ++i)
            {
                if (matchSegments(patternIndex + 1, i))
                {
                    return true;
                }
            }
            return false;
        }

        if (textIndex >= textSegments.size())
        {
            return false;
        }

        if (!MatchGlobSegment(patternSegment, textSegments[textIndex]))
        {
            return false;
        }

        return matchSegments(patternIndex + 1, textIndex + 1);
    };

    return matchSegments(0, 0);
}
}

inline std::string GlobTool(json const& arguments)
{
    auto const pattern = arguments.at("pattern").get_ref<std::string const&>();
    auto const rootDir = arguments.value("root_dir", std::string{"."});

    std::filesystem::path const searchRoot = std::filesystem::absolute(rootDir);
    if (!std::filesystem::exists(searchRoot))
    {
        throw std::runtime_error("error: root directory does not exist");
    }

    std::string const normalizedPattern = StripLeadingRootPrefix(pattern, rootDir);
    std::vector<std::filesystem::path> matches;

    std::error_code error;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(searchRoot, std::filesystem::directory_options::skip_permission_denied, error))
    {
        if (error)
        {
            break;
        }

        if (!entry.is_regular_file(error))
        {
            continue;
        }

        auto const relativePath = std::filesystem::relative(entry.path(), searchRoot);
        std::string const relativeText = NormalizeGlobPath(relativePath.generic_string());
        if (MatchGlobPath(normalizedPattern, relativeText))
        {
            matches.push_back(relativePath);
        }
    }

    std::sort(matches.begin(), matches.end(), [](auto const& left, auto const& right)
    {
        return left.generic_string() < right.generic_string();
    });

    json response
    {
        { "pattern", pattern },
        { "root_dir", rootDir },
        { "matches", json::array() },
    };

    auto& matchArray = response["matches"];
    for (auto const& match : matches)
    {
        matchArray.push_back(match.generic_string());
    }

    return response.dump();
}

constexpr ToolParameter GlobToolParameters[] =
{
    StringToolParameter{ "pattern", "The glob pattern to match, relative to root_dir" },
};

constexpr ToolParameter GlobToolOptionalParameters[] =
{
    StringToolParameter{ "root_dir", "The directory to search from" },
};

constexpr ToolDefinition glob
{
    .name               = "glob",
    .description        = "Find files matching a glob pattern under a directory. Supports *, **, and ?.",
    .requiredParameters = GlobToolParameters,
    .optionalParameters = GlobToolOptionalParameters,
    .callTool           = GlobTool,
};
