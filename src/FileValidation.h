#pragma once

#include "FileUtilities.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class ValidatedFileSystem
{
public:
    ValidatedFileSystem() = default;

    explicit ValidatedFileSystem(std::initializer_list<std::filesystem::path>);
    explicit ValidatedFileSystem(std::filesystem::path const&);
    
    void AddRootPath(std::filesystem::path const&);
    
    [[nodiscard]] std::filesystem::path ValidatePath(std::filesystem::path const&) const;

    std::string ReadTextFile(std::filesystem::path const&) const;
    
    FileChunk ReadTextFileChunk(std::filesystem::path const&, int64_t startLine, int64_t endLine) const;
    
    void WriteTextFile(std::filesystem::path const&, std::string_view content) const;

private:
    std::vector<std::filesystem::path> m_rootPaths;
};
