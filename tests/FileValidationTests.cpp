#include "FileValidation.h"
#include "TestUtilities.h"
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>

namespace fs = std::filesystem;

void TestAddRootPath() {
    std::cout << "Running TestAddRootPath..." << std::endl;
    fs::path tempDir = CreateTempDirectory("test_root");
    
    ValidatedFileSystem vfs(tempDir);
    
    fs::path subDir1 = tempDir / "subdir1";
    fs::create_directory(subDir1);
    vfs.AddRootPath(subDir1);
    
    try {
        (void)vfs.ValidatePath(subDir1);
        std::cout << "  - Added root path successfully." << std::endl;
    } catch (const std::exception& e) {
        Expect(false, "Failed to validate added root path: " + std::string(e.what()));
    }

    fs::remove_all(tempDir);
}

void TestValidatePath() {
    std::cout << "Running TestValidatePath..." << std::endl;
    fs::path tempDir = CreateTempDirectory("test_validate");
    
    fs::path subDir1 = tempDir / "subdir1";
    fs::create_directory(subDir1);
    
    ValidatedFileSystem vfs(tempDir);
    
    // Valid path inside root
    try {
        fs::path validPath = subDir1 / "file.txt";
        fs::path validated = vfs.ValidatePath(validPath);
        ExpectEqual(validated, fs::absolute(validPath).lexically_normal(), "Validated path should match input");
        std::cout << "  - Valid path validation successful." << std::endl;
    } catch (const std::exception& e) {
        Expect(false, "Valid path validation failed: " + std::string(e.what()));
    }

    // Invalid path outside root
    fs::path outsidePath = fs::current_path() / "outside.txt";
    try {
        (void)vfs.ValidatePath(outsidePath);
        Expect(false, "Should have thrown an exception for path outside root");
    } catch (const std::runtime_error& e) {
        std::cout << "  - Caught expected exception for outside path: " << e.what() << std::endl;
    }

    fs::remove_all(tempDir);
}

void TestReadWriteOperations() {
    std::cout << "Running TestReadWriteOperations..." << std::endl;
    fs::path tempDir = CreateTempDirectory("test_rw");
    
    fs::path filePath = tempDir / "test.txt";
    ValidatedFileSystem vfs(tempDir);
    
    // Test Write
    std::string content = "Hello World\nLine 2\nLine 3";
    vfs.WriteTextFile(filePath, content);
    
    // Test Read
    std::string readContent = vfs.ReadTextFile(filePath);
    ExpectEqual(readContent, content, "Read content should match written content");
    std::cout << "  - Read/Write successful." << std::endl;

    // Test ReadChunk
    FileChunk chunk = vfs.ReadTextFileChunk(filePath, 1, 2);
    ExpectEqual(chunk.startLine, 1, "Chunk start line should be 1");
    ExpectEqual(chunk.lines.size(), 2, "Chunk should contain 2 lines");
    if (!chunk.lines.empty()) {
        ExpectEqual(chunk.lines[0], "Hello World", "First line should match");
        ExpectEqual(chunk.lines[1], "Line 2", "Second line should match");
    }
    std::cout << "  - ReadChunk successful." << std::endl;

    fs::remove_all(tempDir);
}

int main() {
    try {
        TestAddRootPath();
        TestValidatePath();
        TestReadWriteOperations();
        std::cout << "All tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test suite failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
