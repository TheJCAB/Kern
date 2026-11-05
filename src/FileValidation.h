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
    
    // Protected paths are readable but never writable: they are how the owners of a workspace
    // declare files that the agent may not change, such as the description of how to build.
    void AddProtectedPath(std::filesystem::path const&);
    
    [[nodiscard]] std::filesystem::path ValidatePath(std::filesystem::path const&) const;

    [[nodiscard]] std::filesystem::path ValidateWritablePath(std::filesystem::path const&) const;

    std::string ReadTextFile(std::filesystem::path const&) const;
    
    FileChunk ReadTextFileChunk(std::filesystem::path const&, int64_t startLine, int64_t endLine) const;
    
    void WriteTextFile(std::filesystem::path const&, std::string_view content) const;

private:
    std::vector<std::filesystem::path> m_rootPaths;
    std::vector<std::filesystem::path> m_protectedPaths;
};
