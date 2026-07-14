
#include "FileUtilities.h"

#if _WIN32
#include <Windows.h>
#endif

import std;
//#include <algorithm>
//#include <exception>
//#include <fstream>
//#include <functional>
//#include <sstream>
//#include <iostream>
//#include <stdexcept>


std::string RawReadTextFile(std::filesystem::path const& path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error{ "error: could not open file" };
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

FileChunk RawReadTextFileChunk(std::filesystem::path const& path, int64_t const startLine, int64_t const endLine)
{
    if (startLine < 1 || endLine < 1 || startLine > endLine)
    {
        return {};
    }

    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error{ "error: could not open file" };
    }

    FileChunk result;
    int64_t lineNumber = 1;
    for (; lineNumber <= endLine; ++lineNumber)
    {
        std::string line;
        if (!std::getline(input, line))
        {
            break;
        }

        if (lineNumber >= startLine)
        {
            result.lines.push_back(std::move(line));
        }
    }

    if (result.lines.empty())
    {
        result.startLine = lineNumber;
    }
    return result;
}

void RawWriteTextFile(std::filesystem::path const& path, std::string_view const content)
{
    auto const parentPath = path.parent_path();
    if (!parentPath.empty() && !std::filesystem::exists(parentPath))
    {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error{ "error: could not write file" };
    }
    output << content;
}

std::filesystem::path GetExecutableDirectory()
{
    static std::filesystem::path const exe_dir =
        []() -> std::filesystem::path
        {
#if _WIN32
            char buffer[MAX_PATH];
            DWORD const size = GetModuleFileNameA(nullptr, buffer, sizeof(buffer));
            if (size > 0 && size < sizeof(buffer))
            {
                return std::filesystem::path{ buffer, buffer + size }.parent_path();
            }
#else
            try
            {
                std::filesystem::path const exe_path = std::filesystem::read_symlink("/proc/self/exe");
                if (!exe_path.empty())
                {
                    return exe_path.parent_path();
                }
            }
            catch (...) {}
#endif

            std::cerr << "error: could not get executable path. Using current path instead." << std::endl;
            return std::filesystem::current_path();
        }
        ();
    return exe_dir;
}


bool MatchGlobSegment(std::filesystem::path::string_type const& pattern, std::filesystem::path::string_type const& text)
{
    using Char = std::filesystem::path::value_type;

    std::size_t patternIndex = 0;
    std::size_t textIndex = 0;
    std::size_t lastStar = pattern.npos;
    std::size_t lastMatch = 0;

    while (textIndex < text.size())
    {
        if (patternIndex < pattern.size() && pattern[patternIndex] == Char{'*'})
        {
            lastStar = patternIndex;
            lastMatch = textIndex;
            ++patternIndex;
        }
        else if (patternIndex < pattern.size() && (pattern[patternIndex] == Char{'?'} || pattern[patternIndex] == text[textIndex]))
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

bool MatchGlobPath(std::filesystem::path const& pattern, std::filesystem::path const& text)
{
    std::vector const patternSegments(pattern.begin(), pattern.end());
    std::vector const textSegments   (text   .begin(), text   .end());

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

std::vector<GlobResult> Glob(std::filesystem::path const& rootDir, std::filesystem::path const& pattern)
{
    std::filesystem::path const canonicalizedRootDir = std::filesystem::canonical(rootDir.lexically_normal());
    std::filesystem::path normalizedPattern = pattern.lexically_normal().lexically_proximate(canonicalizedRootDir);
    if (normalizedPattern.empty() || *normalizedPattern.begin() == "..")
    {
        throw std::runtime_error("error: malformed pattern");
    }

    // Special-case: for now this is implied.
    // TODO: Implement more complete/comprehensive globbing.
    if (*normalizedPattern.begin() == "**")
    {
        normalizedPattern = normalizedPattern.lexically_relative("**");
    }

    std::vector<GlobResult> matches;

    std::error_code error;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(rootDir, std::filesystem::directory_options::skip_permission_denied, error))
    {
        if (error)
        {
            break;
        }

        if (!entry.is_regular_file(error) && !entry.is_directory(error))
        {
            continue;
        }

        auto relativePath = std::filesystem::relative(entry.path(), rootDir);
        if (MatchGlobPath(normalizedPattern, relativePath))
        {
            matches.push_back({
                .name = std::move(relativePath),
                .type = entry.status(error).type(),

            });
        }
    }

    std::sort(matches.begin(), matches.end());

    return matches;
}
