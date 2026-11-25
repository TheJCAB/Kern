#include "BuildCommand.h"
#include "FileValidation.h"
#include "FileUtilities.h"
#include "ToolUtilities.h"
#include "Tools/build.h"

#include "TestUtilities.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
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

// Returns true when the callable throws a std::exception whose message contains `needle`.
template <typename F>
bool ThrowsContaining(F&& f, std::string_view const needle)
{
    try
    {
        f();
    }
    catch (std::exception const& e)
    {
        return std::string_view(e.what()).find(needle) != std::string_view::npos;
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

    std::println("Test Platform Selection");
    {
        std::string const document = R"({"build":{"commands":[{"command":"linux-cmake","platforms":["Linux"]},{"command":"windows-cmake","platforms":["Windows"]},{"command":"mac-cmake","platforms":["macOS"]},{"command":"shared","platforms":["all"]},{"command":"implicit"}]}})";

        BuildCommands const onLinux = ParseBuildConfiguration(document, workspace, "Linux");
        ExpectEqual(onLinux.size(), 3u, "on Linux the declaration must select exactly three commands");
        ExpectEqual(onLinux[0].executable, std::string("linux-cmake"), "on Linux the first selected command must be the Linux one");
        ExpectEqual(onLinux[1].executable, std::string("shared"), "on Linux the second selected command must be the shared one");
        ExpectEqual(onLinux[2].executable, std::string("implicit"), "on Linux the third selected command must be the implicit one: the selection must keep the declaration order");

        BuildCommands const onWindows = ParseBuildConfiguration(document, workspace, "Windows");
        ExpectEqual(onWindows.size(), 3u, "on Windows the declaration must select exactly three commands");
        ExpectEqual(onWindows[0].executable, std::string("windows-cmake"), "on Windows the first selected command must be the Windows one");
        ExpectEqual(onWindows[1].executable, std::string("shared"), "on Windows the second selected command must be the shared one");
        ExpectEqual(onWindows[2].executable, std::string("implicit"), "on Windows the third selected command must be the implicit one: the selection must keep the declaration order");

        BuildCommands const onMac = ParseBuildConfiguration(document, workspace, "macOS");
        ExpectEqual(onMac.size(), 3u, "on macOS the declaration must select exactly three commands");
        ExpectEqual(onMac[0].executable, std::string("mac-cmake"), "on macOS the first selected command must be the macOS one");
        ExpectEqual(onMac[1].executable, std::string("shared"), "on macOS the second selected command must be the shared one");
        ExpectEqual(onMac[2].executable, std::string("implicit"), "on macOS the third selected command must be the implicit one: the selection must keep the declaration order");
    }

    std::println("Test Platform All And Omitted");
    {
        std::string const document = R"({"build":{"commands":[{"command":"shared","platforms":["all"]},{"command":"implicit"}]}})";

        for (std::string const& hostPlatform : { std::string("Windows"), std::string("Linux"), std::string("macOS") })
        {
            BuildCommands const commands = ParseBuildConfiguration(document, workspace, hostPlatform);
            ExpectEqual(commands.size(), 2u, std::format("on {} a command declared for all platforms and a command without platforms must both be selected", hostPlatform));
            ExpectEqual(commands[0].executable, std::string("shared"), std::format("on {} the 'all' command must be selected first", hostPlatform));
            ExpectEqual(commands[1].executable, std::string("implicit"), std::format("on {} the command without platforms must be selected second", hostPlatform));
        }
    }

    std::println("Test Platform On Single Command");
    {
        BuildCommands const commands = ParseBuildConfiguration(R"({"build":{"command":"cmake","platforms":["Linux"]}})", workspace, "Linux");
        ExpectEqual(commands.size(), 1u, "a single command declaration selected for the host must produce one command");
        ExpectEqual(commands.front().executable, std::string("cmake"), "the single command declaration must name the executable");
        Expect(commands.front().arguments.empty(), "the single command declaration must have no arguments");
        Expect(commands.front().timeout == DefaultBuildTimeout, "the single command declaration must use the default timeout");
        Expect(!commands.front().declaredWorkingDirectory, "the single command declaration must not declare a working directory");
        Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","platforms":["Linux"]}})", workspace, "Windows"); }), "a single command selected for another platform must be rejected because no command remains");
    }

    std::println("Test Platform Rejections");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","platforms":["BeOS"]}})", workspace, "Linux"); }), "an unknown platform name must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","platforms":["linux"]}})", workspace, "Linux"); }), "a platform name in the wrong case must be rejected because names are matched exactly");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","platforms":[]}})", workspace, "Linux"); }), "an empty platforms array must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","platforms":"Linux"}})", workspace, "Linux"); }), "a string platforms value must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","platforms":{"Linux":true}}})", workspace, "Linux"); }), "an object platforms value must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","platforms":[1]}})", workspace, "Linux"); }), "a numeric platforms entry must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","platforms":[null]}})", workspace, "Linux"); }), "a null platforms entry must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","platforms":["all","BeOS"]}})", workspace, "Linux"); }), "an unknown platform name next to 'all' must be rejected");
    Expect(Throws([&] { (void)ParseBuildConfiguration(R"({"build":{"commands":[{"command":"cmake"}],"platforms":["Linux"]}})", workspace, "Linux"); }), "platforms outside a command object must be rejected when commands is used");

    std::println("Test Platform Empty Selection");
    {
        Expect(ThrowsContaining([&] { (void)ParseBuildConfiguration(R"({"build":{"command":"cmake","platforms":["Windows"]}})", workspace, "Linux"); }, "Linux"), "an empty selection must be rejected with a message that names the host platform");

        RawWriteTextFile(workspace / BuildConfigurationFileName, R"({"build":{"command":"cmake","platforms":["Windows"]}})");
        Expect(ThrowsContaining([&] { (void)LoadBuildConfiguration(workspace, "Linux"); }, "Linux"), "loading an empty selection must be rejected with a message that names the host platform");
        Expect(ThrowsContaining([&] { (void)LoadBuildConfiguration(workspace, "Linux"); }, BuildConfigurationFileName), "loading an empty selection must be rejected with a message that names the declaration file");
        fs::remove(workspace / BuildConfigurationFileName);
    }
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

    std::println("Test Build Tool Result");
    {
        // The command exits with a known nonzero code on either platform, and its last argument
        // contains a space so that the reported command line must quote it.
        std::string const shell = CurrentPlatform() == "Windows" ? "cmd" : "sh";
        std::string const flag  = CurrentPlatform() == "Windows" ? "/c" : "-c";

        ToolsRuntimeContext context{
            .createNewSession   = {},
            .fs                 = ValidatedFileSystem{ workspace },
            .buildCommands      = BuildCommands{{
                .executable               = shell,
                .arguments                = { flag, "exit 7" },
                .workingDirectory         = std::filesystem::absolute(workspace).lexically_normal(),
                .declaredWorkingDirectory = false,
            }},
            .ignoredDirectories = {},
        };

        json const result = BuildTool(json::object(), context, "");

        Expect(result.contains("command"), "the build result must name the command that was run");
        Expect(result.contains("exitCode"), "the build result must report the process exit code");
        Expect(result.contains("commandIndex"), "the build result must report which command the result is for");
        Expect(result.contains("commandCount"), "the build result must report how many commands were selected");
        Expect(!result.contains("workingDirectory"), "the build result must omit workingDirectory when the command declared none");
        ExpectEqual(result.at("commandCount").get<std::size_t>(), 1u, "a single declared command must be the only selected command");
        ExpectEqual(result.at("commandIndex").get<std::size_t>(), 1u, "the failing command must be reported as the first command");
        ExpectEqual(result.at("exitCode").get<int>(), 7, "the build result must report the exit code of the command that ran");
        ExpectEqual(result.at("ok").get<bool>(), false, "a failing command must not report a successful build");
        ExpectEqual(result.at("status").get<std::string>(), std::string("failed"), "a command that exits nonzero must report the failed status");
        ExpectEqual(result.at("failedCommand").get<std::size_t>(), 1u, "the first command that fails must be reported as failed");

        std::string const command = result.at("command").get<std::string>();
        ExpectEqual(command, std::format("{} {} \"exit 7\"", shell, flag), "the reported command must be a copy-pasteable argv that quotes the argument that contains a space");
        Expect(command.find('=') == std::string::npos, "the reported command must never carry environment variables");

        // The same command succeeds with a declared working directory, which the result must then
        // report, and it must report the successful index the same one-based way.
        fs::path const buildDirectory = workspace / "build-tool-sub";
        fs::create_directory(buildDirectory);

        context.buildCommands = BuildCommands{{
            .executable               = shell,
            .arguments                = { flag, "exit 0" },
            .workingDirectory         = std::filesystem::absolute(buildDirectory).lexically_normal(),
            .declaredWorkingDirectory = true,
        }};

        json const succeeded = BuildTool(json::object(), context, "");

        ExpectEqual(succeeded.at("ok").get<bool>(), true, "a command that exits zero must report a successful build");
        ExpectEqual(succeeded.at("status").get<std::string>(), std::string("succeeded"), "a command that exits zero must report the succeeded status");
        ExpectEqual(succeeded.at("exitCode").get<int>(), 0, "a successful command must report an exit code of zero");
        ExpectEqual(succeeded.at("commandIndex").get<std::size_t>(), 1u, "a successful single command must be reported as the first command");
        ExpectEqual(succeeded.at("commandCount").get<std::size_t>(), 1u, "a successful build must still report how many commands were selected");
        Expect(succeeded.contains("workingDirectory"), "the build result must report the working directory when the command declared one");
        ExpectEqual(std::filesystem::path(succeeded.at("workingDirectory").get<std::string>()).lexically_normal().string(), std::filesystem::absolute(buildDirectory).lexically_normal().string(), "the reported working directory must be the declared one");

        fs::remove_all(buildDirectory);
    }

    fs::remove_all(workspace);

    std::println("All tests passed!");
    return 0;
}
