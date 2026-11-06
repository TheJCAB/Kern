#include "FileValidation.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cwctype>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <type_traits>

namespace
{

using PathChar = std::filesystem::path::value_type;

// Path components are compared as native strings, so on the platforms where the file system itself
// ignores case a difference in case must not be enough to escape a root or a protected path.
PathChar FoldCase(PathChar character)
{
    if constexpr (std::is_same_v<PathChar, char>)
    {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    else
    {
        return static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(character)));
    }
}

bool ComponentsMatch(std::filesystem::path const& left, std::filesystem::path const& right)
{
    if (left == right)
    {
        return true;
    }

    if (!FileSystemPathsIgnoreCase)
    {
        return false;
    }

    std::filesystem::path::string_type const& leftNative  = left.native();
    std::filesystem::path::string_type const& rightNative = right.native();
    if (leftNative.size() != rightNative.size())
    {
        return false;
    }

    return std::ranges::equal(leftNative, rightNative, [](PathChar a, PathChar b)
    {
        return FoldCase(a) == FoldCase(b);
    });
}

// Component-wise prefix test written with explicit iterators, so it never reads past the end of a
// shorter `path`: a path that runs out of components first simply does not contain the prefix.
bool IsSameOrWithin(std::filesystem::path const& prefix, std::filesystem::path const& path)
{
    auto       prefixIt    = prefix.begin();
    auto const prefixEndIt = prefix.end();
    auto       pathIt      = path.begin();
    auto const pathEndIt   = path.end();

    for (; prefixIt != prefixEndIt; ++prefixIt, ++pathIt)
    {
        if (pathIt == pathEndIt)
        {
            return false;
        }

        if (!ComponentsMatch(*prefixIt, *pathIt))
        {
            return false;
        }
    }

    return true;
}

}

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
    // weakly_canonical resolves every symlink in the part of the path that exists and, unlike
    // canonical, does not throw when the rest of the path does not exist yet.
    m_rootPaths.push_back(std::filesystem::weakly_canonical(std::filesystem::absolute(path)));
}

// Helper function to validate that a path is within the global root path.
[[nodiscard]] std::filesystem::path ValidatedFileSystem::ValidatePath(std::filesystem::path const& path) const
{
    // Resolving symlinks is what stops a link inside the workspace from smuggling a read or a write
    // outside of it: the comparison below always sees the real target, never the link.
    std::error_code error;
    std::filesystem::path const absolutePath = std::filesystem::weakly_canonical(std::filesystem::absolute(path), error);
    if (error)
    {
        throw std::runtime_error("Security Error: could not resolve the path " + path.string() + ": " + error.message());
    }

    for (auto const& root : m_rootPaths)
    {
        if (IsWithin(root, absolutePath))
        {
            return absolutePath;
        }
    }

    throw std::runtime_error("Security Error: Access to file outside of root path is denied: " + path.string());
}

void ValidatedFileSystem::AddProtectedPath(std::filesystem::path const& path)
{
    // Unlike a root path this file may not exist yet: weakly_canonical resolves the part that does
    // exist and leaves the rest as it is. Resolving that part anyway makes a protected path below a
    // symlinked ancestor compare equal to an already resolved candidate.
    m_protectedPaths.push_back(std::filesystem::weakly_canonical(std::filesystem::absolute(path)));
}

// A path that is inside a root path is readable, but only some of them are writable.
[[nodiscard]] std::filesystem::path ValidatedFileSystem::ValidateWritablePath(std::filesystem::path const& path) const
{
    std::filesystem::path const absolutePath = ValidatePath(path);

    for (auto const& protectedPath : m_protectedPaths)
    {
        // A protected path also covers everything below it, so a protected ".git" directory covers
        // ".git/hooks/pre-commit" as well.
        if (IsSameOrWithin(protectedPath, absolutePath))
        {
            throw std::runtime_error("Security Error: " + path.string() + " is declared read-only by the owners of this workspace and cannot be modified");
        }
    }

    return absolutePath;
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
    return RawWriteTextFile(ValidateWritablePath(path), content);
}
