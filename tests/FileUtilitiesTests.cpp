#include <FileUtilities.h>

#include "TestUtilities.h"

#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

int main() try
{
    auto const tempDir = CreateTempDirectory("kern_fileutils_test_");
    auto const filePath = tempDir / "sample.txt";

    std::string const expectedContent = "first line\nsecond line\nthird line\n";
    RawWriteTextFile(filePath, expectedContent);

    Expect(std::filesystem::exists(filePath), "expected file to be created");
    ExpectEqual(RawReadTextFile(filePath), expectedContent, "text file content should round-trip");

    auto chunk = RawReadTextFileChunk(filePath, 2, 3);
    ExpectEqual(chunk.lines.size(), std::size_t{2}, "chunk should include requested lines");
    ExpectEqual(chunk.lines[0], std::string{ "second line" }, "first chunk line should be the second line");
    ExpectEqual(chunk.lines[1], std::string{ "third line" }, "second chunk line should be the third line");

    auto missingChunk = RawReadTextFileChunk(filePath, 10, 11);
    Expect(missingChunk.lines.empty(), "missing chunk should be empty");
    ExpectEqual(missingChunk.startLine, int64_t{4}, "missing chunk should report the first line after EOF");

    auto const nestedFile = tempDir / "nested" / "deep" / "nested.txt";
    RawWriteTextFile(nestedFile, "nested");
    Expect(std::filesystem::exists(nestedFile), "nested file should be created");
    ExpectEqual(RawReadTextFile(nestedFile), std::string{ "nested" }, "nested file content should be preserved");

    // Written previously: tempDir / "sample.txt"
    RawWriteTextFile(tempDir / "alpha.txt", "alpha");
    RawWriteTextFile(tempDir / "beta.md", "beta");
    RawWriteTextFile(tempDir / "nested" / "gamma.txt", "gamma");

    {
        auto matches = Glob(tempDir, "*.txt");
        ExpectEqual(matches.size(), std::size_t{2}, "glob should match root-level text files only");
        std::ranges::sort(matches);
        ExpectEqual(matches[0].name, std::filesystem::path{ "alpha.txt" }, "glob should return the expected first root-level match");
        ExpectEqual(matches[1].name, std::filesystem::path{ "sample.txt" }, "glob should return the expected second root-level match");
    }

    std::filesystem::remove_all(tempDir);

    std::cout << "All FileUtilities tests passed!" << std::endl;
    return 0;
}
catch (std::exception const& ex)
{
    std::cerr << "FileUtilities test failed: " << ex.what() << std::endl;
    return 1;
}
