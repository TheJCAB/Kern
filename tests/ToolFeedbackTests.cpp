#include "TestUtilities.h"

#include <FileUtilities.h>
#include <ToolUtilities.h>
#include <Tools/edit_file_lines.h>
#include <Tools/write_file.h>

#include <cstdint>
#include <filesystem>
#include <print>
#include <string>
#include <string_view>

namespace
{
ToolsRuntimeContext MakeContext(std::filesystem::path const& root)
{
    return ToolsRuntimeContext{ .createNewSession = {}, .fs = ValidatedFileSystem{ root }, .buildCommands = {}, .ignoredDirectories = {} };
}

} // namespace

int main()
{
    std::filesystem::path const tempDirectory = CreateTempDirectory("kern_tool_feedback");
    ToolsRuntimeContext   const context       = MakeContext(tempDirectory);

    std::println("Test Expect Mismatch Writes Nothing");
    {
        std::filesystem::path const path     = tempDirectory / "expect_mismatch.txt";
        std::string           const original = "alpha\nbeta\ngamma\ndelta\n";
        RawWriteTextFile(path, original);

        json const result = EditFileLinesTool(json{
            { "path"      , path.string() },
            { "operation" , "replace"     },
            { "start_line", 2             },
            { "content"   , "BETA"        },
            { "expect"    , "betaX"       },
        }, context);

        ExpectEqual(result["success"].get<bool>(), false, "a mismatched expect must report failure");
        ExpectEqual(result.size(), std::size_t{ 9 }, "the mismatch result must carry exactly nine keys");

        Expect(result.contains("path"), "the mismatch result must carry path");
        Expect(result.contains("applied_start"), "the mismatch result must carry applied_start");
        Expect(result.contains("removed_lines"), "the mismatch result must carry removed_lines");
        Expect(result.contains("inserted_lines"), "the mismatch result must carry inserted_lines");
        Expect(result.contains("lines_before"), "the mismatch result must carry lines_before");
        Expect(result.contains("success"), "the mismatch result must carry success");
        Expect(result.contains("error"), "the mismatch result must carry error");
        Expect(result.contains("current_text"), "the mismatch result must carry current_text");
        Expect(result.contains("current_text_truncated"), "the mismatch result must carry current_text_truncated");

        Expect(!result.contains("operation"), "the mismatch result must not echo operation");
        Expect(!result.contains("start_line"), "the mismatch result must not echo start_line");
        Expect(!result.contains("end_line"), "the mismatch result must not echo end_line");
        Expect(!result.contains("context_before"), "the mismatch result must not carry context_before");
        Expect(!result.contains("context_after"), "the mismatch result must not carry context_after");

        ExpectEqual(result["applied_start"].get<std::int64_t>(), std::int64_t{ 2 }, "the mismatch must report the start of the region it inspected");
        ExpectEqual(result["removed_lines"].get<std::int64_t>(), std::int64_t{ 0 }, "a mismatch must remove no lines");
        ExpectEqual(result["inserted_lines"].get<std::int64_t>(), std::int64_t{ 0 }, "a mismatch must insert no lines");
        ExpectEqual(result["lines_before"].get<std::int64_t>(), std::int64_t{ 4 }, "a mismatch must report the unchanged line count");
        ExpectEqual(result["error"].get<std::string>(),
                    std::string("expect did not match: first difference at line 2; file unchanged"),
                    "a mismatch must name the first differing line and state that the file is unchanged");

        json const& currentText = result["current_text"];
        ExpectEqual(currentText.size(), std::size_t{ 3 }, "current_text must start at the first differing line and stop at the end of the file");
        ExpectEqual(currentText[0]["line"].get<std::int64_t>(), std::int64_t{ 2 }, "current_text must start with the first differing line");
        ExpectEqual(currentText[0]["text"].get<std::string>(), std::string("beta"), "current_text must report the current text of the first differing line");
        ExpectEqual(currentText[1]["line"].get<std::int64_t>(), std::int64_t{ 3 }, "current_text must continue with real line numbers");
        ExpectEqual(currentText[2]["line"].get<std::int64_t>(), std::int64_t{ 4 }, "current_text must run to the last line of the file");
        ExpectEqual(result["current_text_truncated"].get<bool>(), false, "current_text of a four line file must not be truncated");
        ExpectEqual(RawReadTextFile(path), original, "a mismatched expect must not write the file");
    }

    std::println("Test Expect Match Proceeds And Echoes Nothing");
    {
        std::filesystem::path const path     = tempDirectory / "expect_match.txt";
        std::string           const original = "alpha\nbeta\ngamma\ndelta\n";
        RawWriteTextFile(path, original);

        json const result = EditFileLinesTool(json{
            { "path"      , path.string() },
            { "operation" , "replace"     },
            { "start_line", 2             },
            { "content"   , "BETA"        },
            { "expect"    , "beta"        },
        }, context);

        ExpectEqual(result["success"].get<bool>(), true, "a matching expect must apply the edit");

        std::string const serialised = result.dump();
        Expect(serialised.find("beta") == std::string::npos, "a successful edit must not echo the replaced text");
        Expect(serialised.find("BETA") == std::string::npos, "a successful edit must not echo the replacement text");

        ExpectEqual(result["applied_start"].get<std::int64_t>(), std::int64_t{ 2 }, "a matching expect must apply at the requested line");
        ExpectEqual(result["removed_lines"].get<std::int64_t>(), std::int64_t{ 1 }, "a single line replace must remove one line");
        ExpectEqual(result["inserted_lines"].get<std::int64_t>(), std::int64_t{ 1 }, "a single line replace must insert one line");
        ExpectEqual(result["lines_before"].get<std::int64_t>(), std::int64_t{ 4 }, "a single line replace must report the old line count");
        ExpectEqual(result["lines_after"].get<std::int64_t>(), std::int64_t{ 4 }, "a single line replace must report the new line count");

        json const& contextBefore = result["context_before"];
        ExpectEqual(contextBefore.size(), std::size_t{ 1 }, "the context before line two must hold the one line above it");
        ExpectEqual(contextBefore[0]["line"].get<std::int64_t>(), std::int64_t{ 1 }, "the context before must carry real line numbers");
        ExpectEqual(contextBefore[0]["text"].get<std::string>(), std::string("alpha"), "the context before must carry the text above the edit");

        json const& contextAfter = result["context_after"];
        ExpectEqual(contextAfter.size(), std::size_t{ 2 }, "the context after line two must hold the two lines below it");
        ExpectEqual(contextAfter[0]["line"].get<std::int64_t>(), std::int64_t{ 3 }, "the context after must carry real line numbers");
        ExpectEqual(contextAfter[0]["text"].get<std::string>(), std::string("gamma"), "the context after must start just below the edit");

        ExpectEqual(RawReadTextFile(path), std::string("alpha\nBETA\ngamma\ndelta\n"), "a matching expect must write the replacement");
    }

    std::println("Test Line Count Invariant");
    {
        std::string const original = "one\ntwo\nthree\nfour\nfive\n";

        auto const expectInvariant = [](json const& result, char const* const message)
        {
            ExpectEqual(result["lines_after"].get<std::int64_t>(),
                        result["lines_before"].get<std::int64_t>() - result["removed_lines"].get<std::int64_t>() + result["inserted_lines"].get<std::int64_t>(),
                        message);
        };

        std::filesystem::path const insertPath = tempDirectory / "invariant_insert.txt";
        RawWriteTextFile(insertPath, original);
        json const insertResult = EditFileLinesTool(json{
            { "path"      , insertPath.string() },
            { "operation" , "insert"            },
            { "start_line", 3                   },
            { "content"   , "x\ny"              },
        }, context);
        ExpectEqual(insertResult["success"].get<bool>(), true, "an insert before line three must succeed");
        ExpectEqual(insertResult["removed_lines"].get<std::int64_t>(), std::int64_t{ 0 }, "an insert must remove no lines");
        ExpectEqual(insertResult["inserted_lines"].get<std::int64_t>(), std::int64_t{ 2 }, "a two line insert must insert two lines");
        expectInvariant(insertResult, "lines_after must equal lines_before - removed_lines + inserted_lines for insert");

        std::filesystem::path const deletePath = tempDirectory / "invariant_delete.txt";
        RawWriteTextFile(deletePath, original);
        json const deleteResult = EditFileLinesTool(json{
            { "path"      , deletePath.string() },
            { "operation" , "delete"            },
            { "start_line", 2                   },
            { "end_line"  , 3                   },
            { "content"   , "x\ny"              },
        }, context);
        ExpectEqual(deleteResult["success"].get<bool>(), true, "deleting lines two to three must succeed");
        ExpectEqual(deleteResult["removed_lines"].get<std::int64_t>(), std::int64_t{ 2 }, "deleting two lines must remove two lines");
        ExpectEqual(deleteResult["inserted_lines"].get<std::int64_t>(), std::int64_t{ 0 }, "a delete must insert no lines");
        expectInvariant(deleteResult, "lines_after must equal lines_before - removed_lines + inserted_lines for delete");

        std::filesystem::path const replacePath = tempDirectory / "invariant_replace.txt";
        RawWriteTextFile(replacePath, original);
        json const replaceResult = EditFileLinesTool(json{
            { "path"      , replacePath.string() },
            { "operation" , "replace"            },
            { "start_line", 4                    },
            { "content"   , "FOUR"               },
        }, context);
        ExpectEqual(replaceResult["success"].get<bool>(), true, "replacing line four must succeed");
        ExpectEqual(replaceResult["removed_lines"].get<std::int64_t>(), std::int64_t{ 1 }, "a single line replace must remove one line");
        ExpectEqual(replaceResult["inserted_lines"].get<std::int64_t>(), std::int64_t{ 1 }, "a single line replace must insert one line");
        expectInvariant(replaceResult, "lines_after must equal lines_before - removed_lines + inserted_lines for replace");

        std::filesystem::path const multiPath = tempDirectory / "invariant_multi.txt";
        RawWriteTextFile(multiPath, original);
        json const multiResult = EditFileLinesTool(json{
            { "path"      , multiPath.string() },
            { "operation" , "replace"          },
            { "start_line", 2                  },
            { "end_line"  , 4                  },
            { "content"   , "a\nb"             },
        }, context);
        ExpectEqual(multiResult["success"].get<bool>(), true, "replacing lines two to four must succeed");
        ExpectEqual(multiResult["removed_lines"].get<std::int64_t>(), std::int64_t{ 3 }, "replacing three lines must remove three lines");
        ExpectEqual(multiResult["inserted_lines"].get<std::int64_t>(), std::int64_t{ 2 }, "replacing with two lines must insert two lines");
        expectInvariant(multiResult, "lines_after must equal lines_before - removed_lines + inserted_lines for a multi-line replace");
        ExpectEqual(RawReadTextFile(multiPath), std::string("one\na\nb\nfive\n"), "a multi-line replace must write the replacement lines in place");
    }

    std::println("Test Omitted End Line Removes One Line");
    {
        std::filesystem::path const path = tempDirectory / "omitted_end_line.txt";
        RawWriteTextFile(path, "one\ntwo\nthree\nfour\nfive\n");

        json const result = EditFileLinesTool(json{
            { "path"      , path.string() },
            { "operation" , "replace"     },
            { "start_line", 3             },
            { "content"   , "THREE"       },
        }, context);

        ExpectEqual(result["success"].get<bool>(), true, "a replace without end_line must succeed");
        ExpectEqual(result["removed_lines"].get<std::int64_t>(), std::int64_t{ 1 }, "a replace without end_line must remove exactly one line");
        ExpectEqual(result["inserted_lines"].get<std::int64_t>(), std::int64_t{ 1 }, "a replace without end_line must insert exactly one line");
        ExpectEqual(result["lines_after"].get<std::int64_t>(), result["lines_before"].get<std::int64_t>(), "a one for one replace must keep the line count");
    }

    std::println("Test Write File Result Size Does Not Scale With Content");
    {
        std::filesystem::path const path = tempDirectory / "many_lines.txt";

        std::string content;
        for (int index = 1; index <= 200; ++index)
        {
            content += "line " + std::to_string(index) + "\n";
        }

        json const manyLinesResult = WriteFileTool(json{
            { "path"   , path.string() },
            { "content", content       },
        }, context);

        ExpectEqual(manyLinesResult["success"].get<bool>(), true, "writing two hundred lines must succeed");
        ExpectEqual(manyLinesResult["created"].get<bool>(), true, "writing a new path must report the file as created");
        ExpectEqual(manyLinesResult["lines_after"].get<std::int64_t>(), std::int64_t{ 200 }, "writing two hundred lines must report two hundred lines");
        ExpectEqual(manyLinesResult.size(), std::size_t{ 4 }, "the write_file result must carry exactly four keys");
        Expect(manyLinesResult.contains("path"), "the write_file result must carry path");
        Expect(manyLinesResult.contains("created"), "the write_file result must carry created");
        Expect(manyLinesResult.contains("lines_after"), "the write_file result must carry lines_after");
        Expect(manyLinesResult.contains("success"), "the write_file result must carry success");

        std::string const manyLinesRaw = RawReadTextFile(path);
        ExpectEqual(CountLines(manyLinesRaw), std::int64_t{ 200 }, "the two hundred line write must put exactly two hundred lines on disk");

        json const oneLineResult = WriteFileTool(json{
            { "path"   , path.string() },
            { "content", "only\n"      },
        }, context);

        ExpectEqual(oneLineResult["success"].get<bool>(), true, "rewriting the path must succeed");
        ExpectEqual(oneLineResult["created"].get<bool>(), false, "rewriting an existing path must not report the file as created");
        ExpectEqual(oneLineResult["lines_after"].get<std::int64_t>(), std::int64_t{ 1 }, "rewriting with one line must report one line");

        std::filesystem::path const editPath = tempDirectory / "size_edit.txt";
        RawWriteTextFile(editPath, "one\ntwo\nthree\n");
        json const editResult = EditFileLinesTool(json{
            { "path"      , editPath.string() },
            { "operation" , "replace"         },
            { "start_line", 2                 },
            { "content"   , "TWO"             },
        }, context);
        ExpectEqual(editResult["success"].get<bool>(), true, "the one line edit used for the size comparison must succeed");

        Expect(manyLinesResult.dump().size() <= editResult.dump().size(), "the write_file result must not grow with the content it wrote");

        std::string const raw = RawReadTextFile(path);
        ExpectEqual(CountLines(raw), std::int64_t{ 1 }, "the path rewritten with one line must hold one line");
        ExpectEqual(CountLines(content), std::int64_t{ 200 }, "the two hundred line content must hold exactly two hundred lines");
    }

    std::println("Test Context Truncation At File Boundaries");
    {
        std::filesystem::path const headPath = tempDirectory / "context_head.txt";
        RawWriteTextFile(headPath, "a\nb\n");
        json const headResult = EditFileLinesTool(json{
            { "path"      , headPath.string() },
            { "operation" , "replace"         },
            { "start_line", 1                 },
            { "content"   , "A"               },
        }, context);

        ExpectEqual(headResult["success"].get<bool>(), true, "replacing the first line must succeed");
        ExpectEqual(headResult["context_before"].size(), std::size_t{ 0 }, "the first line must have no context before it");
        Expect(headResult["context_before"].is_array(), "the context before must be an array even when empty");
        ExpectEqual(headResult["context_truncated"].get<bool>(), true, "an edit at the top of a file must report truncated context");
        ExpectEqual(headResult["context_after"].size(), std::size_t{ 1 }, "the first line of a two line file must be followed by one line");
        ExpectEqual(headResult["context_after"][0]["line"].get<std::int64_t>(), std::int64_t{ 2 }, "the context after must carry real line numbers");

        std::filesystem::path const tailPath = tempDirectory / "context_tail.txt";
        RawWriteTextFile(tailPath, "one\ntwo\nthree\nfour\nfive\n");
        json const tailResult = EditFileLinesTool(json{
            { "path"      , tailPath.string() },
            { "operation" , "replace"         },
            { "start_line", 5                 },
            { "content"   , "FIVE"            },
        }, context);

        ExpectEqual(tailResult["success"].get<bool>(), true, "replacing the last line must succeed");
        ExpectEqual(tailResult["context_after"].size(), std::size_t{ 0 }, "the last line must have no context after it");
        Expect(tailResult["context_after"].is_array(), "the context after must be an array even when empty");
        ExpectEqual(tailResult["context_truncated"].get<bool>(), true, "an edit at the bottom of a file must report truncated context");

        json const& tailBefore = tailResult["context_before"];
        ExpectEqual(tailBefore.size(), std::size_t{ 3 }, "the last line of a five line file must be preceded by three context lines");
        ExpectEqual(tailBefore[0]["line"].get<std::int64_t>(), std::int64_t{ 2 }, "the context before must carry real line numbers");
        ExpectEqual(tailBefore[0]["text"].get<std::string>(), std::string("two"), "the context before must carry the real text above the edit");
        ExpectEqual(tailBefore[1]["line"].get<std::int64_t>(), std::int64_t{ 3 }, "the context before must carry consecutive line numbers");
        ExpectEqual(tailBefore[2]["line"].get<std::int64_t>(), std::int64_t{ 4 }, "the context before must end just above the edit");

        std::filesystem::path const middlePath = tempDirectory / "context_middle.txt";
        RawWriteTextFile(middlePath, "1\n2\n3\n4\n5\n6\n7\n");
        json const middleResult = EditFileLinesTool(json{
            { "path"      , middlePath.string() },
            { "operation" , "replace"           },
            { "start_line", 4                   },
            { "content"   , "FOUR"              },
        }, context);

        ExpectEqual(middleResult["success"].get<bool>(), true, "replacing a middle line must succeed");
        ExpectEqual(middleResult["context_before"].size(), std::size_t{ 3 }, "a middle edit must be preceded by three context lines");
        ExpectEqual(middleResult["context_after"].size(), std::size_t{ 3 }, "a middle edit must be followed by three context lines");
        ExpectEqual(middleResult["context_truncated"].get<bool>(), false, "a middle edit of a long enough file must report full context");
    }

    std::println("Test Newline Added");
    {
        std::filesystem::path const trailingPath = tempDirectory / "newline_trailing.txt";
        RawWriteTextFile(trailingPath, "a\nb\n");
        json const trailingResult = EditFileLinesTool(json{
            { "path"      , trailingPath.string() },
            { "operation" , "replace"             },
            { "start_line", 1                     },
            { "content"   , "A"                   },
        }, context);

        ExpectEqual(trailingResult["success"].get<bool>(), true, "replacing the first line must succeed");
        ExpectEqual(trailingResult["newline_added"].get<bool>(), true, "an edit of a file that ended with a newline must keep the newline");
        std::string const trailingText = RawReadTextFile(trailingPath);
        Expect(trailingText.ends_with('\n'), "the rewritten file must still end with a newline");

        std::filesystem::path const barePath = tempDirectory / "newline_bare.txt";
        RawWriteTextFile(barePath, "a\nb");
        json const bareResult = EditFileLinesTool(json{
            { "path"      , barePath.string() },
            { "operation" , "replace"         },
            { "start_line", 1                 },
            { "content"   , "A"               },
        }, context);

        ExpectEqual(bareResult["success"].get<bool>(), true, "replacing the first line of a file without a trailing newline must succeed");
        ExpectEqual(bareResult["newline_added"].get<bool>(), false, "an edit of a file that did not end with a newline must not add one");
        Expect(!RawReadTextFile(barePath).ends_with('\n'), "the rewritten file must not gain a trailing newline");
    }

    std::println("Test Validation Wording");
    {
        std::filesystem::path const path = tempDirectory / "validation.txt";
        RawWriteTextFile(path, "one\ntwo\n");

        json const result = EditFileLinesTool(json{
            { "path"      , path.string() },
            { "operation" , "replace"     },
            { "start_line", 0             },
            { "content"   , "ONE"         },
        }, context);

        ExpectEqual(result["success"].get<bool>(), false, "start_line zero must fail");
        ExpectEqual(result.size(), std::size_t{ 3 }, "the validation failure must carry exactly three keys");
        Expect(result.contains("path"), "the validation failure must carry path");
        Expect(result.contains("success"), "the validation failure must carry success");
        Expect(result.contains("error"), "the validation failure must carry error");
        ExpectEqual(result["error"].get<std::string>(), std::string("start_line must be at least 1"), "the validation failure must name start_line and its minimum");
    }

    std::println("All tests passed!");

    return 0;
}
