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

    auto namesOf =
        [](GlobMatches const& matches)
        {
            std::vector<std::filesystem::path> names;
            for (auto const& match : matches.matches)
            {
                names.push_back(match.name);
            }
            std::sort(names.begin(), names.end());
            return names;
        };

    auto runGlob =
        [&](std::string_view const pattern, GlobOptions const& options = {})
        {
            return Glob(tempDir, pattern, options);
        };

    {
        GlobMatches const txtMatches = runGlob("*.txt");
        std::vector<std::filesystem::path> const expected{
            "alpha.txt",
            "sample.txt",
        };
        std::map<std::string, std::size_t> const expectedCounts{ { ".", 2 } };
        ExpectEqual(namesOf(txtMatches), expected, "a bare pattern matches from rootDir only, so *.txt should return only top-level txt files");
        ExpectEqual(txtMatches.totalMatches, txtMatches.matches.size(), "totalMatches should equal matches.size() when nothing is capped");
        ExpectEqual(txtMatches.countsByTopLevelDirectory, expectedCounts, "*.txt should count both matches as directly under rootDir");
    }

    {
        GlobMatches const txtMatches = runGlob("**/*.txt");
        std::vector<std::filesystem::path> const expected{
            "alpha.txt",
            "dir/nested/deep.txt",
            "dir/top.txt",
            "nested/deep/nested.txt",
            "nested/gamma.txt",
            "sample.txt",
        };
        std::map<std::string, std::size_t> const expectedCounts{ { ".", 2 }, { "dir", 2 }, { "nested", 2 } };
        ExpectEqual(namesOf(txtMatches), expected, "'**/' is how the whole tree is searched, so **/*.txt should return txt files at any depth");
        ExpectEqual(txtMatches.totalMatches, txtMatches.matches.size(), "totalMatches should equal matches.size() when nothing is capped");
        ExpectEqual(txtMatches.countsByTopLevelDirectory, expectedCounts, "**/*.txt should count matches per top-level directory");
    }

    {
        auto names = namesOf(runGlob("**/notes*"));
        std::vector<std::filesystem::path> const expected{
            "dir/notes.md"
        };
        ExpectEqual(names, expected, "glob should match files with wildcard at the end under an explicit '**/' prefix");
    }

    {
        auto names = namesOf(runGlob("**/*otes.md"));
        std::vector<std::filesystem::path> const expected{
            "dir/notes.md"
        };
        ExpectEqual(names, expected, "glob should match files with wildcards at the start under an explicit '**/' prefix");
    }

    {
        auto names = namesOf(runGlob("**/*otes*"));
        std::vector<std::filesystem::path> const expected{
            "dir/notes.md"
        };
        ExpectEqual(names, expected, "glob should match files with wildcards at both ends under an explicit '**/' prefix");
    }

    {
        auto names = namesOf(runGlob("**/no*s.md"));
        std::vector<std::filesystem::path> const expected{
            "dir/notes.md"
        };
        ExpectEqual(names, expected, "glob should match files with wildcard in the middle under an explicit '**/' prefix");
    }

    {
        auto names = namesOf(runGlob("**/file"));
        std::vector<std::filesystem::path> const expected{
            "file",
            "nested/file",
        };
        ExpectEqual(names, expected, "glob should match root-level and nested files with **/file");
    }

    {
        auto names = namesOf(runGlob("/file"));
        std::vector<std::filesystem::path> const expected{
            "file",
        };
        ExpectEqual(names, expected, "glob should match root-level only files with /file)");
    }

    {
        auto names = namesOf(runGlob("/**/file"));
        std::vector<std::filesystem::path> const expected{
            "file",
            "nested/file",
        };
        ExpectEqual(names, expected, "glob should match root-level and nested files with /**/file)");
    }

    {
        auto names = namesOf(runGlob("dir/**/*.txt"));
        std::vector<std::filesystem::path> const expected{
            "dir/nested/deep.txt",
            "dir/top.txt",
        };
        ExpectEqual(names, expected, "glob should match txt files under dir at any depth");
    }

    {
        auto names = namesOf(runGlob("dir/*/deep.txt"));
        std::vector<std::filesystem::path> const expected{
            "dir/nested/deep.txt"
        };
        ExpectEqual(names, expected, "glob should match top.txt inside a subdirectory of dir");
    }

    {
        auto names = namesOf(runGlob("nonexistent_file_pattern"));
        Expect(names.empty(), "glob should return empty vector for non-existent pattern");
    }

    {
        auto names = namesOf(runGlob("dir/nonexistent_file.txt"));
        Expect(names.empty(), "glob should return empty vector for non-existent path under dir");
    }

    {
        auto names = namesOf(runGlob("non_existent_folder/file.txt"));
        Expect(names.empty(), "glob should return empty vector for path with non-existent directory");
    }

    // The ignore set and the cap live in their own directory, so the shared tempDir cases above stay intact.
    {
        std::filesystem::path const ignoreDir = CreateTempDirectory("kern_fileutils_ignore_");
        RawWriteTextFile(ignoreDir / "top.txt");
        RawWriteTextFile(ignoreDir / "nested" / "kept.txt");
        RawWriteTextFile(ignoreDir / "build" / "generated.txt");
        RawWriteTextFile(ignoreDir / ".git" / "config.txt");

        std::vector<std::filesystem::path> const nestedAndTop{
            "nested/kept.txt",
            "top.txt",
        };
        std::vector<std::string> const expectedIgnored{ "build", ".git" };

        GlobMatches const defaultIgnored = Glob(ignoreDir, "**/*.txt", { .ignoredDirectories = { "build", ".git" } });
        ExpectEqual(namesOf(defaultIgnored), nestedAndTop, "the default ignore set shape should hide the build and .git artefacts");
        ExpectEqual(defaultIgnored.ignoredDirectories, expectedIgnored, "the applied ignore set should be reported in first-seen order");

        GlobMatches const deduplicated = Glob(ignoreDir, "**/*.txt", { .ignoredDirectories = { "build", "build", ".git" } });
        ExpectEqual(namesOf(deduplicated), nestedAndTop, "a repeated ignore entry should still hide the same artefacts");
        ExpectEqual(deduplicated.ignoredDirectories, expectedIgnored, "a repeated ignore entry should be deduplicated in the reported set");

        GlobMatches const nestedIgnored = Glob(ignoreDir, "**/*.txt", { .ignoredDirectories = { "nested" } });
        std::vector<std::filesystem::path> const expectedNestedIgnored{
            ".git/config.txt",
            "build/generated.txt",
            "top.txt",
        };
        ExpectEqual(namesOf(nestedIgnored), expectedNestedIgnored, "the ignore set should be configurable, so ignoring only nested leaves .git searched");

        GlobMatches const escaped = Glob(ignoreDir / "build", "**/*.txt", { .ignoredDirectories = { "build" } });
        std::vector<std::filesystem::path> const expectedEscaped{
            "generated.txt"
        };
        ExpectEqual(namesOf(escaped), expectedEscaped, "the ignore set should never apply to rootDir itself, so aiming at build still searches it");

        // "**/*" also yields the directories it walks, so the real total is five: top.txt, nested, build,
        // nested/kept.txt and build/generated.txt. ".git" is ignored to keep the count deterministic.
        GlobMatches const capped = Glob(ignoreDir, "**/*", { .ignoredDirectories = { ".git" }, .maxResults = 2 });
        ExpectEqual(capped.matches.size(), std::size_t{ 2 }, "maxResults should cap the number of returned matches");
        ExpectEqual(capped.totalMatches, std::size_t{ 5 }, "totalMatches should report every match found before capping");

        std::filesystem::remove_all(ignoreDir);
    }

    // Symlinks are never followed by a "**" search, so a link cannot make a glob walk out of the tree.
    // This uses its own temp directories so the shared tempDir fixture stays untouched.
    {
        std::filesystem::path const linkRoot    = CreateTempDirectory("kern_fileutils_link_");
        std::filesystem::path const linkOutside = CreateTempDirectory("kern_fileutils_link_outside_");

        RawWriteTextFile(linkRoot / "kept.txt");
        RawWriteTextFile(linkRoot / "real" / "child.txt");
        RawWriteTextFile(linkOutside / "escaped.txt");

        std::error_code dirLinkError;
        std::filesystem::create_symlink(linkOutside, linkRoot / "dir_link", dirLinkError);
        std::error_code fileLinkError;
        std::filesystem::create_symlink(linkOutside / "escaped.txt", linkRoot / "escaped.txt", fileLinkError);

        if (dirLinkError || fileLinkError)
        {
            std::cout << "Symlinks are not permitted for this user on this platform, so the symlink glob test was skipped." << std::endl;
        }
        else
        {
            std::vector<std::filesystem::path> const expected{
                "escaped.txt",
                "kept.txt",
                "real/child.txt",
            };
            ExpectEqual(namesOf(Glob(linkRoot, "**/*.txt")), expected, "a '**' search should report a symlinked file as a match but never descend into a symlinked directory");

            Expect(namesOf(Glob(linkRoot, "dir_link/*.txt")).empty(), "descent should stop at a symlinked directory even when the pattern names it explicitly");
        }

        std::filesystem::remove_all(linkRoot);
        std::filesystem::remove_all(linkOutside);
    }

    {
        bool threw = false;
        try
        {
            Glob(tempDir, "*.{txt,md}");
        }
        catch (UnsupportedGlobPatternError const&)
        {
            threw = true;
        }
        Expect(threw, "a pattern with braces should throw UnsupportedGlobPatternError");
    }

    {
        bool threw = false;
        try
        {
            Glob(tempDir, "[ab]*.txt");
        }
        catch (UnsupportedGlobPatternError const&)
        {
            threw = true;
        }
        Expect(threw, "a pattern with character classes should throw UnsupportedGlobPatternError");
    }

    {
        // A pattern such as "nested/../*.txt" normalises to "*.txt" and therefore cannot throw, so this
        // case uses a leading ".." instead: lexically_normal() preserves a leading ".." segment.
        bool threw = false;
        try
        {
            Glob(tempDir, "../*.txt");
        }
        catch (InvalidGlobPatternError const&)
        {
            threw = true;
        }
        Expect(threw, "a pattern with a '..' segment should throw InvalidGlobPatternError");
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
