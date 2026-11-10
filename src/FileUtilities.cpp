
#include "FileUtilities.h"

#if _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <exception>
#include <fstream>
#include <functional>
#include <generator>
#include <print>
#include <ranges>
#include <span>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>


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

    FileChunk result { .startLine = startLine, .eof = false };
    int64_t lineNumber = 1;
    for (; lineNumber <= endLine; ++lineNumber)
    {
        std::string line;
        if (!std::getline(input, line))
        {
            result.eof = true;
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

    if (!result.eof)
    {
        // Find whether we are at the end of the file.
        std::string line;
        if (!std::getline(input, line))
        {
            result.eof = true;
        }
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

    // Write to a sibling temporary file and rename it over the destination once the
    // content is safely on disk. If anything fails, the temporary file is removed and
    // the destination is left untouched, so a failed write can never leave a
    // truncated file or a stray temporary behind.
    class TemporaryFileGuard
    {
    public:
        explicit TemporaryFileGuard(std::filesystem::path temporaryPath) : m_path(std::move(temporaryPath)) {}
        TemporaryFileGuard(TemporaryFileGuard const&) = delete;
        TemporaryFileGuard& operator=(TemporaryFileGuard const&) = delete;
        ~TemporaryFileGuard()
        {
            if (!m_committed)
            {
                std::error_code ignored;
                std::filesystem::remove(m_path, ignored);
            }
        }
        void Commit() { m_committed = true; }

    private:
        std::filesystem::path m_path;
        bool                  m_committed = false;
    };

    std::filesystem::path temporaryPath = path;
    temporaryPath += ".tmp";
    TemporaryFileGuard guard{ temporaryPath };

    {
        std::ofstream output(temporaryPath, std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error{ "error: could not write file" };
        }
        output << content;
        output.flush();
        if (!output)
        {
            throw std::runtime_error{ "error: could not write file" };
        }
    }

    std::error_code renameError;
    std::filesystem::rename(temporaryPath, path, renameError);
    if (renameError)
    {
        throw std::runtime_error{ "error: could not write file: " + renameError.message() };
    }

    guard.Commit();
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

            std::println("error: could not get executable path. Using current path instead.");
            return std::filesystem::current_path();
        }
        ();
    return exe_dir;
}

// True when `candidate` is `root` itself or lies below it. The comparison is component-wise, so
// "/ws/build" is inside "/ws" while "/ws/build2" is not, and a candidate shorter than the root is
// simply not inside it.
bool IsWithin(std::filesystem::path const& root, std::filesystem::path const& candidate)
{
    std::filesystem::path const rootPath      = std::filesystem::absolute(root).lexically_normal();
    std::filesystem::path const candidatePath = std::filesystem::absolute(candidate).lexically_normal();

    auto const mismatch = std::mismatch(rootPath.begin(), rootPath.end(), candidatePath.begin(), candidatePath.end());
    return mismatch.first == rootPath.end();
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

bool IsIgnoredDirectory(std::filesystem::path const& entryName, std::span<std::string const> ignoredDirectories)
{
    for (std::string const& ignored : ignoredDirectories)
    {
        if (entryName == std::filesystem::path{ ignored })
        {
            return true;
        }
    }
    return false;
}

// Recursive generator.
std::generator<GlobResult> GlobInternal(std::filesystem::path rootDir, std::ranges::input_range auto const& pattern, bool recurseAll = false, std::span<std::string const> ignoredDirectories = {})
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
            co_yield std::ranges::elements_of(GlobInternal(rootDir, std::ranges::subrange{ &star, &star + 1 }, true, ignoredDirectories));
        }
        co_return;
    }

    auto const nextSegmentIt = std::next(segmentIt);

    auto const& segment = *segmentIt;
    if (segment == ".."_path)
    {
        throw InvalidGlobPatternError("parameter 'pattern' contains a '..' segment, which is not supported");
    }

    for (auto const& entry : std::filesystem::directory_iterator(rootDir, std::filesystem::directory_options::skip_permission_denied, error))
    {
        auto const entryName = entry.path().filename();

        // A symlink is never followed: its target can be outside the tree being searched, so
        // descending through one would make a glob silently walk out of the workspace. A symlinked
        // file is still reported as a match (the reader validates the resolved path again), but a
        // symlinked directory is a leaf and recursion stops there.
        std::error_code symlinkError;
        bool const      isSymlink = entry.is_symlink(symlinkError);

        // The ignore set applies to entries below rootDir only: this loop never sees rootDir itself.
        if (entry.is_directory(error) && IsIgnoredDirectory(entryName, ignoredDirectories))
        {
            continue;
        }

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
            else if (!isSymlink && entry.is_directory(error))
            {
                // Note: even if recurseAll is set, we've now matched a newer segment.
                // So we must consider the recursion "spent" and not continue the recursion here.
                co_yield std::ranges::elements_of(GlobInternal(rootDir / entryName, std::ranges::subrange{ nextSegmentIt, segmentEndIt }, false, ignoredDirectories));
            }
        }

        if (recurseAll && !isSymlink && entry.is_directory(error))
        {
            // recurseAll means we search in every subdirectory, whether it matched or not.
            // Note: we swallow this directory into the recurseAll pattern and keep searching,
            // which makes this recursion different from when we recursed a match above.
            co_yield std::ranges::elements_of(GlobInternal(rootDir / entryName, std::ranges::subrange{ segmentIt, segmentEndIt }, true, ignoredDirectories));
        }
    }
}

GlobMatches Glob(std::filesystem::path const& rootDir, std::string_view const pattern, GlobOptions const& options)
{
    std::filesystem::path normalizedRootDir = rootDir.lexically_normal();
    if (!std::filesystem::exists(normalizedRootDir) || !std::filesystem::is_directory(normalizedRootDir))
    {
        return {};
    }

    // Reject unsupported constructs in the raw pattern, before any normalisation, so the diagnostic
    // names exactly what the caller typed. Braces are checked first so a pattern with both reports braces.
    if (pattern.find('{') != std::string_view::npos)
    {
        throw UnsupportedGlobPatternError("parameter 'pattern' uses braces, which are not supported; the pattern language is '*', '?', '**' and literal text");
    }
    if (pattern.find('[') != std::string_view::npos)
    {
        throw UnsupportedGlobPatternError("parameter 'pattern' uses character classes, which are not supported; the pattern language is '*', '?', '**' and literal text");
    }

    // Deduplicate the ignore set, keeping the first-seen order so the applied set reads like the input.
    std::vector<std::string> appliedIgnoredDirectories;
    for (std::string const& ignored : options.ignoredDirectories)
    {
        if (std::ranges::find(appliedIgnoredDirectories, ignored) == appliedIgnoredDirectories.end())
        {
            appliedIgnoredDirectories.push_back(ignored);
        }
    }

    // TODO: Use char8_t for UTF-8 content consistently so we don't have to do this here.
    std::u8string_view const utf8Pattern{ reinterpret_cast<char8_t const*>(pattern.data()), pattern.size() };
    auto normalizedPattern = std::filesystem::path{ utf8Pattern, std::filesystem::path::format::generic_format }.lexically_normal();
    if (normalizedPattern.has_root_name())
    {
        throw InvalidGlobPatternError("parameter 'pattern' must be relative to root_dir");
    }
    if (normalizedPattern.has_root_directory())
    {
        normalizedPattern = normalizedPattern.relative_path();
    }

    std::vector<GlobResult> matches;
    for (GlobResult globResult : GlobInternal(normalizedRootDir, normalizedPattern, false, appliedIgnoredDirectories))
    {
        globResult.name = globResult.name.lexically_relative(normalizedRootDir);
        matches.push_back(std::move(globResult));
    }

    // Sort and eliminate any duplicates.
    // Duplicates can happen if we get multiple distinct "**" in the pattern.
    std::ranges::sort(matches);
    {
        auto const [ begin, end ] = std::ranges::unique(matches);
        matches.erase(begin, end);
    }

    // Report over the whole result set, before any capping.
    GlobMatches result;
    result.totalMatches       = matches.size();
    result.ignoredDirectories = std::move(appliedIgnoredDirectories);
    for (GlobResult const& match : matches)
    {
        // A match directly in rootDir has a single path element and is counted under ".".
        std::string const key = match.name.has_parent_path() ? match.name.begin()->string() : ".";
        ++result.countsByTopLevelDirectory[key];
    }

    if (options.maxResults != 0 && matches.size() > options.maxResults)
    {
        matches.resize(options.maxResults);
    }
    result.matches = std::move(matches);

    return result;
}