#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

std::string RawReadTextFile(std::filesystem::path const& path);

struct FileChunk
{
    int64_t                  startLine = 1;
    std::vector<std::string> lines     {};
    bool                     eof       = false;
};

// TODO: Implement this as streaming via std::generator.
FileChunk RawReadTextFileChunk(std::filesystem::path const& path, int64_t startLine, int64_t endLine);

void RawWriteTextFile(std::filesystem::path const& path, std::string_view content);

inline void RawWriteTextFile(std::filesystem::path const& path) { RawWriteTextFile(path, path.string()); }


std::filesystem::path GetExecutableDirectory();

struct GlobResult
{
    std::filesystem::path      name;
    std::filesystem::file_type type;

    friend auto operator== (GlobResult const& a, GlobResult const& b) noexcept { return std::tie(a.name, a.type) ==  std::tie(b.name, b.type); }
    friend auto operator<=>(GlobResult const& a, GlobResult const& b) noexcept { return std::tie(a.name, a.type) <=> std::tie(b.name, b.type); }
};

// Exceptions distinguish the failure modes the tools report as errorCode values.
struct GlobPatternError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// The pattern uses a construct outside the supported language ('{' or '[').
struct UnsupportedGlobPatternError : GlobPatternError
{
    using GlobPatternError::GlobPatternError;
};

// The pattern is malformed (root name, or a ".." segment).
struct InvalidGlobPatternError : GlobPatternError
{
    using GlobPatternError::GlobPatternError;
};

struct GlobOptions
{
    std::vector<std::string> ignoredDirectories {};  // Bare directory names, applied to entries below rootDir, never to rootDir itself.
    std::size_t              maxResults = 0;         // 0 means unlimited.
};

struct GlobMatches
{
    std::vector<GlobResult>            matches            {};  // Capped to GlobOptions::maxResults.
    std::size_t                        totalMatches       = 0;  // Number of distinct matches before capping.
    std::vector<std::string>           ignoredDirectories {};  // The set that was actually applied, deduplicated, in first-seen order.
    std::map<std::string, std::size_t> countsByTopLevelDirectory {};  // Every match before capping, keyed by its first path segment below rootDir ("." for entries directly in rootDir).
};

GlobMatches Glob(std::filesystem::path const& rootDir, std::string_view pattern, GlobOptions const& options = {});
