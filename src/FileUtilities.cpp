
#include "FileUtilities.h"

#if _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <exception>
#include <fstream>
#include <functional>
#include <generator>
#include <ranges>
#include <sstream>
#include <iostream>
#include <stdexcept>


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

    while (patternIndex < pattern.size() && pattern[patternIndex] == Char{'*'})
    {
        ++patternIndex;
    }

    return patternIndex == pattern.size();
}

// Utility function to generate path-native strings (using wchar_t on Windows and char on Linux).
std::filesystem::path::string_type operator""_path(char const* string, size_t size)
{
    std::filesystem::path result{ string, string + size };
    return result.native();
}

// Recursive generator.
std::generator<GlobResult> GlobInternal(std::filesystem::path rootDir, std::ranges::input_range auto const& pattern, bool recurseAll = false)
{
    std::error_code error;

    auto segmentEndIt = std::ranges::end(pattern);

    auto segmentIt     = std::ranges::begin(pattern);
    
    while (segmentIt != segmentEndIt && *segmentIt == "**"_path)
    {
        // Note: this is benign if we encounter redundant repetitions like "**/**"
        recurseAll    = true;
        segmentIt     = std::next(segmentIt);
    }

    if (segmentIt == segmentEndIt)
    {
        if (recurseAll)
        {
            // "**" as the end of the search implies a "/*"
            std::filesystem::path::string_type const star = "*"_path;
            co_yield std::ranges::elements_of(GlobInternal(rootDir, std::ranges::subrange{ &star, &star + 1 }, true));
        }
        co_return;
    }

    auto const nextSegmentIt = std::next(segmentIt);

    auto const& segment = *segmentIt;
    if (segment == ".."_path)
    {
        throw std::runtime_error("error: malformed pattern");
    }

    for (auto const& entry : std::filesystem::directory_iterator(rootDir, std::filesystem::directory_options::skip_permission_denied, error))
    {
        auto const entryName = entry.path().filename();
        if (MatchGlobSegment(segment, entryName))
        {
            if (nextSegmentIt == segmentEndIt)
            {
                // Found a leaf. File or directory, doesn't matter, it's a hit.
                co_yield GlobResult{
                    .name = rootDir / entryName,
                    .type = entry.status(error).type(),
                };
            }
            else if (entry.is_directory(error))
            {
                // Note: even if recurseAll is set, we've now matched a newer segment.
                // So we must consider the recursion "spent" and not continue the recursion here.
                co_yield std::ranges::elements_of(GlobInternal(rootDir / entryName, std::ranges::subrange{ nextSegmentIt, segmentEndIt }, false));
            }
        }

        if (recurseAll && entry.is_directory(error))
        {
            // recurseAll means we search in every subdirectory, whether it matched or not.
            // Note: we swallow this directory into the recurseAll pattern and keep searching,
            // which makes this recursion different from when we recursed a match above.
            co_yield std::ranges::elements_of(GlobInternal(rootDir / entryName, std::ranges::subrange{ segmentIt, segmentEndIt }, true));
        }
    }
}

std::vector<GlobResult> Glob(std::filesystem::path const& rootDir, std::string_view const pattern)
{
    std::filesystem::path normalizedRootDir = rootDir.lexically_normal();
    if (!std::filesystem::exists(normalizedRootDir) || !std::filesystem::is_directory(normalizedRootDir))
    {
        return {};
    }

    // TODO: Use char8_t for UTF-8 content consistently so we don't have to do this here.
    std::u8string_view const utf8Pattern{ reinterpret_cast<char8_t const*>(pattern.data()), pattern.size() };
    auto normalizedPattern = std::filesystem::path{ utf8Pattern, std::filesystem::path::format::generic_format }.lexically_normal();
    if (normalizedPattern.has_root_name())
    {
        throw std::runtime_error("error: malformed pattern");
    }
    if (normalizedPattern.has_root_directory())
    {
        normalizedPattern = normalizedPattern.relative_path();
    }
    else
    {
        normalizedPattern = "**" / normalizedPattern;
    }

    std::vector<GlobResult> result;
    for (GlobResult globResult : GlobInternal(normalizedRootDir, normalizedPattern))
    {
        globResult.name = globResult.name.lexically_relative(normalizedRootDir);
        result.push_back(globResult);
    }

    // Sort and eliminate any duplicates.
    // Duplicates can happen if we get multiple distinct "**" in the pattern.
    std::ranges::sort(result);
    {
        auto const [ begin, end ] = std::ranges::unique(result);
        result.erase(begin, end);
    }

    return result;
}
