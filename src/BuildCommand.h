#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The file, at the root of the workspace, in which the workspace owners declare that Kern
// may build the project and exactly how to do it. Kern may read it but never write it.
constexpr char BuildConfigurationFileName[] = "Kern.build.json";

constexpr std::chrono::seconds DefaultBuildTimeout { 600 };
constexpr std::chrono::seconds MaximumBuildTimeout { 3600 };
// The name of the platform this binary was built for, which is what a command's optional
// 'platforms' selection is compared against: "Windows", "macOS" or "Linux".
constexpr std::string_view CurrentPlatform()
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Linux";
#endif
}

// A single, fully resolved command that the workspace owners declared.
// It is run directly, never through a shell, and never interpolates model-provided text.
struct BuildCommand
{
    std::string              executable;
    std::vector<std::string> arguments;
    std::filesystem::path    workingDirectory;
    // True when the declaration named a working directory, so that a build result may report it.
    bool                     declaredWorkingDirectory = false;
    std::chrono::seconds     timeout{ DefaultBuildTimeout };
};

// The commands of one declaration, in order. A build stops at the first command that fails.
using BuildCommands = std::vector<BuildCommand>;

// Returns std::nullopt when the workspace does not declare a build, which means that no
// build tool exists at all. A declaration either names a single 'command' or a non-empty
// 'commands' array. Each command object may narrow itself to some platforms with an
// optional 'platforms' array whose entries are exactly "Windows", "Linux", "macOS" or
// "all"; omitting the key means every platform. Only the commands selected for
// `hostPlatform` are returned, in declaration order.
// Throws std::runtime_error when the declaration exists but is not valid, including when
// the selection leaves no command at all for `hostPlatform`, since an empty build would
// make the build tool silently do nothing.
std::optional<BuildCommands> LoadBuildConfiguration(std::filesystem::path const& workspaceRoot, std::string_view hostPlatform = CurrentPlatform());

// Throws std::runtime_error when `jsonText` is not a valid build declaration. A declaration
// either names a single 'command' or a non-empty 'commands' array, and a command's optional
// 'platforms' array selects which of them apply to `hostPlatform`. The result holds only the
// selected commands, in declaration order, and throwing beats returning none of them.
BuildCommands ParseBuildConfiguration(std::string_view jsonText, std::filesystem::path const& workspaceRoot, std::string_view hostPlatform = CurrentPlatform());
