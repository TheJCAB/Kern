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

// A single, fully resolved command that the workspace owners declared.
// It is run directly, never through a shell, and never interpolates model-provided text.
struct BuildCommand
{
    std::string              executable;
    std::vector<std::string> arguments;
    std::filesystem::path    workingDirectory;
    std::chrono::seconds     timeout{ DefaultBuildTimeout };
};

// The commands of one declaration, in order. A build stops at the first command that fails.
using BuildCommands = std::vector<BuildCommand>;

// Returns std::nullopt when the workspace does not declare a build, which means that no
// build tool exists at all. A declaration either names a single 'command' or a non-empty
// 'commands' array.
// Throws std::runtime_error when the declaration exists but is not valid.
std::optional<BuildCommands> LoadBuildConfiguration(std::filesystem::path const& workspaceRoot);

// Throws std::runtime_error when `jsonText` is not a valid build declaration. A declaration
// either names a single 'command' or a non-empty 'commands' array.
BuildCommands ParseBuildConfiguration(std::string_view jsonText, std::filesystem::path const& workspaceRoot);
