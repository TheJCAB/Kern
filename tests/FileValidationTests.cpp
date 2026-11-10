#include "FileValidation.h"
#include "FileUtilities.h"
#include "TestUtilities.h"
#include <iostream>
#include <filesystem>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

// Runs `callable` and fails unless it throws the std::runtime_error the validator raises when it
// denies access. Written once so every negative case reads as the single assertion it is.
void ExpectRuntimeError(auto&& callable, std::string_view const message) {
    try {
        callable();
    } catch (const std::runtime_error&) {
        return;
    }

    Expect(false, message);
}

}

void TestAddRootPath() {
    std::println("Running TestAddRootPath...");
    fs::path tempDir = CreateTempDirectory("test_root");
    
    ValidatedFileSystem vfs(tempDir);
    
    fs::path subDir1 = tempDir / "subdir1";
    fs::create_directory(subDir1);
    vfs.AddRootPath(subDir1);
    
    try {
        (void)vfs.ValidatePath(subDir1);
        std::println("  - Added root path successfully.");
    } catch (const std::exception& e) {
        Expect(false, "Failed to validate added root path: " + std::string(e.what()));
    }

    fs::remove_all(tempDir);
}

void TestValidatePath() {
    std::println("Running TestValidatePath...");
    fs::path tempDir = CreateTempDirectory("test_validate");
    
    fs::path subDir1 = tempDir / "subdir1";
    fs::create_directory(subDir1);
    
    ValidatedFileSystem vfs(tempDir);
    
    // Valid path inside root
    try {
        fs::path validPath = subDir1 / "file.txt";
        fs::path validated = vfs.ValidatePath(validPath);
        ExpectEqual(validated, fs::weakly_canonical(fs::absolute(validPath)), "Validated path should match the input: validation resolves symlinks, so the returned path is the resolved one");
        std::println("  - Valid path validation successful.");
    } catch (const std::exception& e) {
        Expect(false, "Valid path validation failed: " + std::string(e.what()));
    }

    // Invalid path outside root
    fs::path outsidePath = fs::current_path() / "outside.txt";
    try {
        (void)vfs.ValidatePath(outsidePath);
        Expect(false, "Should have thrown an exception for path outside root");
    } catch (const std::runtime_error& e) {
        std::println("  - Caught expected exception for outside path: {}", e.what());
    }

    fs::remove_all(tempDir);
}

void TestReadWriteOperations() {
    std::println("Running TestReadWriteOperations...");
    fs::path tempDir = CreateTempDirectory("test_rw");
    
    fs::path filePath = tempDir / "test.txt";
    ValidatedFileSystem vfs(tempDir);
    
    // Test Write
    std::string content = "Hello World\nLine 2\nLine 3";
    vfs.WriteTextFile(filePath, content);
    
    // Test Read
    std::string readContent = vfs.ReadTextFile(filePath);
    ExpectEqual(readContent, content, "Read content should match written content");
    std::println("  - Read/Write successful.");

    // Test ReadChunk
    FileChunk chunk = vfs.ReadTextFileChunk(filePath, 1, 2);
    ExpectEqual(chunk.startLine, 1, "Chunk start line should be 1");
    ExpectEqual(chunk.lines.size(), 2u, "Chunk should contain 2 lines");
    if (!chunk.lines.empty()) {
        ExpectEqual(chunk.lines[0], "Hello World", "First line should match");
        ExpectEqual(chunk.lines[1], "Line 2", "Second line should match");
    }
    std::println("  - ReadChunk successful.");

    fs::remove_all(tempDir);
}

void TestValidatePathShorterThanRoot() {
    std::println("Running TestValidatePathShorterThanRoot...");
    fs::path tempDir = CreateTempDirectory("test_shorter_root_");
    fs::path inner = tempDir / "inner";
    fs::path deeper = inner / "deeper";
    fs::create_directories(deeper);

    ValidatedFileSystem vfs(deeper);

    // A candidate two components shorter than the root is the case the old 3-iterator std::mismatch
    // read past the end of: the candidate runs out of components first and must simply be rejected.
    ExpectRuntimeError([&] { (void)vfs.ValidatePath(tempDir); }, "a path short by two components must be rejected as outside the root");
    ExpectRuntimeError([&] { (void)vfs.ValidatePath(inner); }, "a path short by one component must be rejected as outside the root");
    ExpectRuntimeError([&] { (void)vfs.ValidatePath(fs::path{} / ".."); }, "a path that climbs out of the root with '..' must be rejected");
    std::println("  - Short paths rejected.");

    // A not-yet-existing file directly under the root is still inside it: weakly_canonical resolves the
    // part that exists and leaves the rest, so validation succeeds without requiring the file to exist.
    fs::path const candidateFile = deeper / "file.txt";
    ExpectEqual(vfs.ValidatePath(candidateFile), fs::weakly_canonical(candidateFile), "a not-yet-existing file directly under the root should validate to its resolved path");
    ExpectEqual(vfs.ValidatePath(deeper), fs::weakly_canonical(deeper), "the root path itself should validate to its resolved path");
    std::println("  - Root and paths below it accepted.");

    fs::remove_all(tempDir);
}

void TestSymlinkEscape() {
    std::println("Running TestSymlinkEscape...");
    fs::path rootDir = CreateTempDirectory("test_link_root_");
    fs::path outsideDir = CreateTempDirectory("test_link_outside_");

    fs::path const secretPath = outsideDir / "secret.txt";
    RawWriteTextFile(secretPath, "top secret\n");

    std::error_code fileLinkError;
    fs::create_symlink(secretPath, rootDir / "file_link.txt", fileLinkError);
    std::error_code dirLinkError;
    fs::create_symlink(outsideDir, rootDir / "dir_link", dirLinkError);

    if (fileLinkError || dirLinkError) {
        std::println("  - Symlinks are not permitted for this user on this platform, so the symlink escape test was skipped.");
        fs::remove_all(rootDir);
        fs::remove_all(outsideDir);
        return;
    }

    ValidatedFileSystem vfs(rootDir);

    // The link itself is inside the root, but the target it resolves to is not, so every operation
    // that resolves the path must refuse it.
    ExpectRuntimeError([&] { (void)vfs.ValidatePath(rootDir / "file_link.txt"); }, "a symlinked file pointing outside the root must be rejected by ValidatePath");
    ExpectRuntimeError([&] { (void)vfs.ValidatePath(rootDir / "dir_link" / "secret.txt"); }, "a file reached through a symlinked directory pointing outside the root must be rejected by ValidatePath");
    ExpectRuntimeError([&] { (void)vfs.ValidateWritablePath(rootDir / "dir_link" / "secret.txt"); }, "a path reached through a symlinked directory pointing outside the root must be rejected by ValidateWritablePath");
    ExpectRuntimeError([&] { (void)vfs.ReadTextFile(rootDir / "file_link.txt"); }, "reading through a symlink pointing outside the root must be rejected");
    std::println("  - Escaping symlinks rejected.");

    // A symlink whose target stays inside the root is not an escape and must keep working.
    std::string const insideContent = "inside the root\n";
    RawWriteTextFile(rootDir / "inside.txt", insideContent);
    std::error_code insideLinkError;
    fs::create_symlink(rootDir / "inside.txt", rootDir / "inside_link.txt", insideLinkError);
    if (insideLinkError) {
        std::println("  - Could not create an inside symlink, so that check was skipped.");
        fs::remove_all(rootDir);
        fs::remove_all(outsideDir);
        return;
    }

    ExpectEqual(vfs.ReadTextFile(rootDir / "inside_link.txt"), insideContent, "a symlink whose target is inside the root should still be readable");
    std::println("  - Inside symlink accepted.");

    fs::remove_all(rootDir);
    fs::remove_all(outsideDir);
}

void TestProtectedPaths() {
    std::println("Running TestProtectedPaths...");
    fs::path tempDir = CreateTempDirectory("test_protected_");

    fs::path const protectedDir = tempDir / "protected";
    fs::create_directories(protectedDir);
    RawWriteTextFile(protectedDir / "guard.txt", "guarded\n");
    RawWriteTextFile(tempDir / "Kern.build.json", "{}\n");

    ValidatedFileSystem vfs(tempDir);
    vfs.AddProtectedPath(tempDir / "Kern.build.json");
    vfs.AddProtectedPath(protectedDir);

    ExpectRuntimeError([&] { (void)vfs.ValidateWritablePath(tempDir / "Kern.build.json"); }, "a protected path must not be writable");
    // ".." must be normalised away before the comparison, so this is the protected file even though
    // the intermediate directory does not exist.
    ExpectRuntimeError([&] { (void)vfs.ValidateWritablePath(tempDir / "sub" / ".." / "Kern.build.json"); }, "a protected path reached through a non-existent directory and '..' must still not be writable");
    ExpectRuntimeError([&] { (void)vfs.ValidateWritablePath(protectedDir / "hooks" / "pre-commit"); }, "a protected directory covers everything below it");
    std::println("  - Writes to protected paths rejected.");

    ExpectEqual(vfs.ValidateWritablePath(tempDir / "other.txt"), fs::weakly_canonical(tempDir / "other.txt"), "an unrelated path should stay writable");
    ExpectEqual(vfs.ValidateWritablePath(tempDir / "protected2" / "file.txt"), fs::weakly_canonical(tempDir / "protected2" / "file.txt"), "a sibling whose name merely starts with the protected directory name should stay writable");
    std::println("  - Unprotected paths accepted as writable.");

    // Protected means read-only, not unreadable.
    ExpectEqual(vfs.ReadTextFile(tempDir / "Kern.build.json"), std::string{ "{}\n" }, "a protected path must still be readable");
    ExpectEqual(vfs.ValidatePath(tempDir / "Kern.build.json"), fs::weakly_canonical(tempDir / "Kern.build.json"), "a protected path must still validate for reading");
    std::println("  - Protected paths still readable.");

    fs::remove_all(tempDir);
}

void TestProtectedPathCaseSensitivity() {
    std::println("Running TestProtectedPathCaseSensitivity...");
    fs::path tempDir = CreateTempDirectory("test_protected_case_");
    RawWriteTextFile(tempDir / "Kern.build.json", "{}\n");

    ValidatedFileSystem vfs(tempDir);
    vfs.AddProtectedPath(tempDir / "Kern.build.json");

    if constexpr (FileSystemPathsIgnoreCase) {
        // On this file system both names resolve to one directory entry, so accepting the lower-case
        // spelling would be a way to write the protected file.
        ExpectRuntimeError([&] { (void)vfs.ValidateWritablePath(tempDir / "kern.build.json"); }, "on a case-insensitive file system a differently-cased spelling of a protected path must not be writable");
        std::println("  - Case-insensitive spellings of a protected path rejected.");
    } else {
        // On a case-sensitive file system this is a genuinely different file, which is not protected.
        ExpectEqual(vfs.ValidateWritablePath(tempDir / "kern.build.json"), fs::weakly_canonical(tempDir / "kern.build.json"), "on a case-sensitive file system a differently-cased name is a different, unprotected file");
        std::println("  - Differently-cased name is a different file and stays writable.");
    }

    fs::remove_all(tempDir);
}

int main() {
    try {
        TestAddRootPath();
        TestValidatePath();
        TestValidatePathShorterThanRoot();
        TestSymlinkEscape();
        TestProtectedPaths();
        TestProtectedPathCaseSensitivity();
        TestReadWriteOperations();
        std::println("All tests passed!");
    } catch (const std::exception& e) {
        std::println("Test suite failed with exception: {}", e.what());
        return 1;
    }
    return 0;
}