#include "FileUtilities.h"
#include "RuntimePaths.h"

#include "TestUtilities.h"

#include <filesystem>
#include <print>

int main()
{
    std::println("Test GetRuntimeDataDirectory");

    // A test binary runs from the build tree, which has no installed data directory beside it, so the directory next to the executable is the one that applies. It is returned whether or not the directory exists yet, because a missing data directory is a startup error rather than a path lookup question.
    std::filesystem::path const dataDirectory = GetRuntimeDataDirectory();
    ExpectEqual(dataDirectory, GetExecutableDirectory() / "data", "a build tree reads its data beside the executable");
    Expect(dataDirectory.is_absolute(), "the data directory is absolute");

    std::println("Test GetDefaultLogDirectory");

    // The same applies to the log directory: only an installed program writes it outside its own tree, and an installed program is not what a test binary is.
    std::filesystem::path const logDirectory = GetDefaultLogDirectory();
    ExpectEqual(logDirectory, GetExecutableDirectory() / "logs", "a build tree logs beside the executable");
    Expect(logDirectory.is_absolute(), "the log directory is absolute");

    // The two directories are separate, so that runtime data and run artifacts are never confused for one another.
    ExpectEqual(logDirectory, dataDirectory.parent_path() / "logs", "the log directory sits beside the data directory");
    Expect(logDirectory != dataDirectory, "data and logs are distinct directories");

    std::println("All tests passed!");
    return 0;
}
