#include <FileUtilities.h>

#include "TestUtilities.h"

#include <algorithm>
#include <iostream>
#include <map>
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
    std::map<std::string, std::filesystem::file_type> const globFiles
    {
        { "alpha.txt"           , std::filesystem::file_type::regular   },
        { "beta.md"             , std::filesystem::file_type::regular   },
        { "nested"              , std::filesystem::file_type::directory },
        { "nested/gamma.txt"    , std::filesystem::file_type::regular   },
        { "file"                , std::filesystem::file_type::regular   },
        { "nested/file"         , std::filesystem::file_type::regular   },
        { "empty"               , std::filesystem::file_type::directory },
        { "dir"                 , std::filesystem::file_type::directory },
        { "dir/nested"          , std::filesystem::file_type::directory },
        { "dir/top.txt"         , std::filesystem::file_type::regular   },
        { "dir/nested/deep.txt" , std::filesystem::file_type::regular   },
        { "dir/notes.md"        , std::filesystem::file_type::regular   },
    };

    for (auto& [path, type] : globFiles)
    {
        if (type == std::filesystem::file_type::regular)
        {
            RawWriteTextFile(tempDir / path );
        }
    }

    auto runGlob =
        [&](std::string_view const pattern)
        {
            auto matches = Glob(tempDir, pattern);
            std::vector<std::filesystem::path> names;
            for (auto const& match : matches)
            {
                names.push_back(match.name);
            }
            std::sort(names.begin(), names.end());
            return names;
        };

    {
        auto names = runGlob("*.txt");
        std::vector<std::filesystem::path> const expected{
            "alpha.txt",
            "dir/nested/deep.txt",
            "dir/top.txt",
            "nested/deep/nested.txt",
            "nested/gamma.txt",
            "sample.txt",
        };
        ExpectEqual(names.size(), std::size_t{6}, "glob should match txt files at any depth when the pattern has no slash");
        ExpectEqual(names, expected, "glob should return the expected txt files for a git-style pattern without a slash");
    }

    {
        auto names = runGlob("notes*");
        std::vector<std::filesystem::path> const expected{
            "dir/notes.md"
        };
        ExpectEqual(names, expected, "glob should match files with wildcard at the end");
    }

    {
        auto names = runGlob("*otes.md");
        std::vector<std::filesystem::path> const expected{
            "dir/notes.md"
        };
        ExpectEqual(names, expected, "glob should match files with wildcards at the start");
    }

    {
        auto names = runGlob("*otes*");
        std::vector<std::filesystem::path> const expected{
            "dir/notes.md"
        };
        ExpectEqual(names, expected, "glob should match files with wildcards at both ends");
    }

    {
        auto names = runGlob("no*s.md");
        std::vector<std::filesystem::path> const expected{
            "dir/notes.md"
        };
        ExpectEqual(names, expected, "glob should match files with wildcard in the middle");
    }

    {
        auto names = runGlob("**/file");
        std::vector<std::filesystem::path> const expected{
            "file",
            "nested/file",
        };
        ExpectEqual(names, expected, "glob should match root-level and nested files with **/file");
    }

    {
        auto names = runGlob("/file");
        std::vector<std::filesystem::path> const expected{
            "file",
        };
        ExpectEqual(names, expected, "glob should match root-level only files with /file)");
    }

    {
        auto names = runGlob("/**/file");
        std::vector<std::filesystem::path> const expected{
            "file",
            "nested/file",
        };
        ExpectEqual(names, expected, "glob should match root-level and nested files with /**/file)");
    }

    {
        auto names = runGlob("dir/**/*.txt");
        std::vector<std::filesystem::path> const expected{
            "dir/nested/deep.txt",
            "dir/top.txt",
        };
        ExpectEqual(names, expected, "glob should match txt files under dir at any depth");
    }

    {
        auto names = runGlob("dir/*/deep.txt");
        std::vector<std::filesystem::path> const expected{
            "dir/nested/deep.txt"
        };
        ExpectEqual(names, expected, "glob should match top.txt inside a subdirectory of dir");
    }

    {
        auto names = runGlob("nonexistent_file_pattern");
        Expect(names.empty(), "glob should return empty vector for non-existent pattern");
    }

    {
        auto names = runGlob("dir/nonexistent_file.txt");
        Expect(names.empty(), "glob should return empty vector for non-existent path under dir");
    }

    {
        auto names = runGlob("non_existent_folder/file.txt");
        Expect(names.empty(), "glob should return empty vector for path with non-existent directory");
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
