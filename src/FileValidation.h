#pragma once

#include "FileUtilities.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
// Version control metadata lives in a directory of this name at the root of a workspace. It is never
// writable: a hook installed there is a command that runs the next time a human runs git, which is
// exactly what a guardrail must refuse.
constexpr char VersionControlDirectoryName[] = ".git";

// Windows and macOS resolve "kern.build.json" and "Kern.build.json" to the same directory entry, so
// on those platforms every path comparison in the guarded file system ignores case. Elsewhere the two
// names are genuinely different files, and an exact comparison is the correct one.
#if defined(_WIN32) || defined(__APPLE__)
constexpr bool FileSystemPathsIgnoreCase = true;
#else
constexpr bool FileSystemPathsIgnoreCase = false;
#endif

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
