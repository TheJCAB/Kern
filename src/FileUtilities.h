#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

std::string ReadTextFile(std::filesystem::path const& path);

struct FileChunk
{
    int64_t                  startLine = 1;
    std::vector<std::string> lines;
};

FileChunk ReadTextFileChunk(std::filesystem::path const& path, int64_t startLine, int64_t endLine);

void WriteTextFile(std::filesystem::path const& path, std::string_view content);

std::filesystem::path GetExecutableDirectory();