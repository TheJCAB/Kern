#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

std::string RawReadTextFile(std::filesystem::path const& path);

struct FileChunk
{
    int64_t                  startLine = 1;
    std::vector<std::string> lines;
};

FileChunk RawReadTextFileChunk(std::filesystem::path const& path, int64_t startLine, int64_t endLine);

void RawWriteTextFile(std::filesystem::path const& path, std::string_view content);

std::filesystem::path GetExecutableDirectory();

struct GlobResult
{
    std::filesystem::path      name;
    std::filesystem::file_type type;

    friend auto operator== (GlobResult const& a, GlobResult const& b) noexcept { return std::tie(a.name, a.type) ==  std::tie(b.name, b.type); }
    friend auto operator<=>(GlobResult const& a, GlobResult const& b) noexcept { return std::tie(a.name, a.type) <=> std::tie(b.name, b.type); }
};

std::vector<GlobResult> Glob(std::filesystem::path const& rootDir, std::filesystem::path const& pattern);
