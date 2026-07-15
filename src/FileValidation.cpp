#include "FileValidation.h"

ValidatedFileSystem::ValidatedFileSystem(std::initializer_list<std::filesystem::path> const paths)
{
    for (auto& path : paths)
    {
        AddRootPath(path);
    }
}

ValidatedFileSystem::ValidatedFileSystem(std::filesystem::path const& path)
{
    AddRootPath(path);
}

void ValidatedFileSystem::AddRootPath(std::filesystem::path const& path)
{
    m_rootPaths.push_back(std::filesystem::canonical(path.lexically_normal()));
}

// Helper function to validate that a path is within the global root path.
[[nodiscard]] std::filesystem::path ValidatedFileSystem::ValidatePath(std::filesystem::path const& path) const
{
    //std::filesystem::path const absolutePath = std::filesystem::canonical(path.lexically_normal());
    std::filesystem::path const absolutePath = std::filesystem::absolute(path).lexically_normal();

    for (auto& root : m_rootPaths)
    {
        auto [rootEnd, pathEnd] = std::mismatch(root.begin(), root.end(), absolutePath.begin());
        if (rootEnd == root.end())
        {
            return absolutePath;
        }
    }

    throw std::runtime_error("Security Error: Access to file outside of root path is denied: " + path.string());
}

std::string ValidatedFileSystem::ReadTextFile(std::filesystem::path const& path) const
{
    return RawReadTextFile(ValidatePath(path));
}

FileChunk ValidatedFileSystem::ReadTextFileChunk(std::filesystem::path const& path, int64_t startLine, int64_t endLine) const
{
    return RawReadTextFileChunk(ValidatePath(path), startLine, endLine);
}

void ValidatedFileSystem::WriteTextFile(std::filesystem::path const& path, std::string_view content) const
{
    return RawWriteTextFile(ValidatePath(path), content);
}
