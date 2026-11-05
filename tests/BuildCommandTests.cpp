#include "BuildCommand.h"
#include "FileValidation.h"
#include "FileUtilities.h"

#include "TestUtilities.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

template <typename F>
bool Throws(F&& f)
{
    try
    {
        f();
    }
    catch (std::exception const&)
    {
        return true;
    }
    return false;
}

} // namespace

int main()
{
    fs::path const workspace = CreateTempDirectory("kern_build_config");

    std::println("Test Minimal Declaration");
    {
        BuildCommands const commands = ParseBuildConfiguration(R"({"build":{"command":"cmake"}})", workspace);
        ExpectEqual(commands.size(), 1u, "a single command declaration must produce one command");
        ExpectEqual(commands.front().executable, std::string("cmake"), "the minimal declaration must name the executable");
        Expect(commands.front().arguments.empty(), "the minimal declaration must have no arguments");
        ExpectEqual(commands.front().workingDirectory.string(), fs::absolute(workspace).lexically_normal().string(), "the minimal declaration must work in the workspace root");
        Expect(commands.front().timeout == DefaultBuildTimeout, "the minimal declaration must use the default timeout");
    }

    std::println("Test Full Declaration");
    {
        fs::path const sub = workspace / "sub";
        fs::create_directory(sub);

        BuildCommands const commands = ParseBuildConfiguration(R"({"build":{"command":"cmake","arguments":["--build","--preset","Clang-Release"],"timeoutSeconds":120,"workingDirectory":"sub"}})", workspace);
        ExpectEqual(commands.size(), 1u, "a single command declaration must produce one command");
        ExpectEqual(commands.front().executable, std::string("cmake"), "the full declaration must name the executable");
        ExpectEqual(commands.front().arguments.size(), 3u, "the full declaration must have three arguments");
        Expect(commands.front().arguments[1] == "--preset", "the arguments must keep their order");
        Expect(commands.front().timeout == std::chrono::seconds{ 120 }, "the full declaration must use the declared timeout");
        Expect(!commands.front().workingDirectory.empty(), "the working directory must not be empty");
        ExpectEqual(commands.front().workingDirectory.filename().string(), std::string("sub"), "the working directory must be the declared subdirectory");

        fs::remove_all(sub);
    }

    std::println("Test Multiple Commands");
    {
        BuildCommands const commands = ParseBuildConfiguration(R"({"build":{"commands":[{"command":"cmake","arguments":["--preset","Clang-Release"]},{"command":"cmake","arguments":["--build","--preset","Clang-Release"],"timeoutSeconds":30}]}})", workspace);
        ExpectEqual(commands.size(), 2u, "a commands declaration must keep every command");
        ExpectEqual(commands[0].executable, std::string("cmake"), "the first command must name the executable");
        ExpectEqual(commands[0].arguments.size(), 2u, "the first command must have two arguments");
        ExpectEqual(commands[1].arguments.front(), std::string("--build"), "the commands must keep their order");
        Expect(commands[1].timeout == std::chrono::seconds{ 30 }, "each command must use its own timeout");
        Expect(commands[0].timeout == DefaultBuildTimeout, "a command without a timeout must use the default timeout");
        ExpectEqual(commands[1].workingDirectory.string(), fs::absolute(workspace).lexically_normal().string(), "a command without a working directory must work in the workspace root");

        BuildCommands const single = ParseBuildConfiguration(R"({"build":{"commands":[{"command":"ninja"}]}})", workspace);
        ExpectEqual(single.size(), 1u, "a single-element commands array must produce one command");
        ExpectEqual(single.front().executable, std::string("ninja"), "a single-element commands declaration must name the executable");
    }

    std::println("Test Rejections");
    Expect(Throws([&] { (void)ParseBuildConfiguration("not json", workspace); }), "invalid JSON must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"([1,2])", workspace); }), "a JSON array must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"other":{}})", workspace); }), "an unknown top-level key must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({})", workspace); }), "a missing build object must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":3})", workspace); }), "a non-object build must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","shell":"bash -c"}})", workspace); }), "an unknown key inside build must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{}})", workspace); }), "a missing command must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":""}})", workspace); }), "an empty command must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":3}})", workspace); }), "a non-string command must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","arguments":"x"}})", workspace); }), "non-array arguments must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","arguments":["a",1]}})", workspace); }), "non-string arguments must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","workingDirectory":".."}})", workspace); }), "a working directory outside the workspace must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","workingDirectory":"does-not-exist"}})", workspace); }), "a missing working directory must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","timeoutSeconds":0}})", workspace); }), "a zero timeout must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","timeoutSeconds":1.5}})", workspace); }), "a fractional timeout must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","timeoutSeconds":3601}})", workspace); }), "a timeout above the maximum must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","commands":[{"command":"ninja"}]}})", workspace); }), "declaring both command and commands must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"commands":[]}})", workspace); }), "an empty commands array must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"commands":"cmake"}})", workspace); }), "a non-array commands value must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"commands":["cmake"]}})", workspace); }), "non-object command entries must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"commands":[{"arguments":[]}]}})", workspace); }), "a commands entry without a command must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"commands":[{"command":"cmake","shell":"bash -c"}]}})", workspace); }), "an unknown key inside a commands entry must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"commands":[{"command":"cmake"},{"command":"ninja","workingDirectory":".."}]}})", workspace); }), "a command whose working directory is outside the workspace must be rejected");
    ExpectEqual(ParseBuildConfiguration(R"({"build":{"commands":[{"command":"a"},{"command":"a"},{"command":"a"},{"command":"a"},{"command":"a"},{"command":"a"},{"command":"a"},{"command":"a"}]}})", workspace).size(), 8u, "all the commands must be accepted");

    std::println("Test Missing Declaration");
    {
        std::optional<BuildCommands> const loaded = LoadBuildConfiguration(workspace);
        Expect(!loaded.has_value(), "a workspace without a declaration must not declare a build");
    }

    std::println("Test Load Declaration");
    fs::path const configurationPath = workspace / BuildConfigurationFileName;
    RawWriteTextFile(configurationPath, R"({"build":{"command":"ninja"}})");
    {
        std::optional<BuildCommands> const loaded = LoadBuildConfiguration(workspace);
        Expect(loaded.has_value(), "a workspace with a valid declaration must declare a build");
        if (loaded.has_value())
        {
            ExpectEqual(loaded->size(), 1u, "the loaded declaration must declare one command");
            ExpectEqual(loaded->front().executable, std::string("ninja"), "the loaded declaration must name the executable");
        }
    }

    std::println("Test Reject Invalid Declaration File");
    RawWriteTextFile(configurationPath, "{");
    Expect(Throws([&] { (void)LoadBuildConfiguration(workspace); }), "an invalid declaration file must be rejected");

    std::println("Test Protected Path");
    {
        RawWriteTextFile(configurationPath, R"({"build":{"command":"ninja"}})");

        ValidatedFileSystem fs{ workspace };
        fs.AddProtectedPath(workspace / BuildConfigurationFileName);
        Expect(Throws([&] { fs.WriteTextFile(workspace / BuildConfigurationFileName, "x"); }), "a protected path cannot be written");
        Expect(!fs.ReadTextFile(workspace / BuildConfigurationFileName).empty(), "a protected path can still be read");
    }

    fs::remove_all(workspace);

    std::println("All tests passed!");
    return 0;
}
