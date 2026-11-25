#include "BuildCommand.h"

#include "FileUtilities.h"
#include "JsonUtilities.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr char const* KnownBuildCommandKeys[] = { "command", "arguments", "workingDirectory", "timeoutSeconds", "platforms" };

constexpr char const* KnownBuildKeys[] = { "command", "arguments", "workingDirectory", "timeoutSeconds", "commands" };

// A command exactly as one entry of the declaration described it, with the platforms it named
// kept so that the selection can be made before anything runs.
struct DeclaredBuildCommand
{
    BuildCommand             command;
    std::vector<std::string> platforms; // Empty when the declaration named none, meaning every platform.
};

// The platform names a declaration may use. They are matched exactly, never case-insensitively.
bool IsKnownPlatform(std::string_view const name)
{
    return name == "Windows" || name == "Linux" || name == "macOS" || name == "all";
}

bool IsSelectedForPlatform(DeclaredBuildCommand const& declared, std::string_view const hostPlatform)
{
    if (declared.platforms.empty())
    {
        return true;
    }

    return std::ranges::any_of(declared.platforms, [hostPlatform](std::string const& platform)
    {
        return platform == "all" || platform == hostPlatform;
    });
}

std::string ReadRequiredString(json const& object, char const* key)
{
    auto const it = object.find(key);
    if (it == object.end() || !it->is_string())
    {
        throw std::runtime_error(std::format("'{}' must be a string", key));
    }

    std::string const value = it->get<std::string>();
    if (value.empty())
    {
        throw std::runtime_error(std::format("'{}' must not be empty", key));
    }

    return value;
}

// Parses and validates one declared command out of a build declaration object.
DeclaredBuildCommand ParseBuildCommand(json const& object, std::filesystem::path const& workspaceRoot)
{
    for (auto const& [key, value] : object.items())
    {
        bool const known = std::ranges::any_of(std::begin(KnownBuildCommandKeys), std::end(KnownBuildCommandKeys), [&key](char const* candidate)
        {
            return key == candidate;
        });
        if (!known)
        {
            throw std::runtime_error(std::format("Unknown key '{}' in a build command", key));
        }
    }

    DeclaredBuildCommand declared;

    declared.command.executable = ReadRequiredString(object, "command");

    auto const argumentsIt = object.find("arguments");
    if (argumentsIt != object.end() && !argumentsIt->is_null())
    {
        if (!argumentsIt->is_array())
        {
            throw std::runtime_error("'arguments' must be an array of strings");
        }
        for (auto const& element : *argumentsIt)
        {
            if (!element.is_string())
            {
                throw std::runtime_error("'arguments' must be an array of strings");
            }
            declared.command.arguments.push_back(element.get<std::string>());
        }
    }

    auto const directoryIt = object.find("workingDirectory");
    if (directoryIt != object.end() && !directoryIt->is_null())
    {
        if (!directoryIt->is_string())
        {
            throw std::runtime_error("'workingDirectory' must be a string");
        }

        std::filesystem::path const directory = directoryIt->get<std::string>();
        declared.command.workingDirectory = (directory.is_absolute() ? directory : workspaceRoot / directory).lexically_normal();
        declared.command.workingDirectory = std::filesystem::absolute(declared.command.workingDirectory).lexically_normal();

        if (!IsWithin(workspaceRoot, declared.command.workingDirectory))
        {
            throw std::runtime_error("'workingDirectory' must be inside the workspace");
        }

        std::error_code error;
        if (!std::filesystem::is_directory(declared.command.workingDirectory, error))
        {
            throw std::runtime_error(std::format("'workingDirectory' is not a directory: {}", declared.command.workingDirectory.string()));
        }

        declared.command.declaredWorkingDirectory = true;
    }
    else
    {
        declared.command.workingDirectory = std::filesystem::absolute(workspaceRoot).lexically_normal();
    }

    auto const timeoutIt = object.find("timeoutSeconds");
    if (timeoutIt != object.end() && !timeoutIt->is_null())
    {
        if (!timeoutIt->is_number_integer())
        {
            throw std::runtime_error("'timeoutSeconds' must be a positive integer");
        }

        auto const seconds = timeoutIt->get<std::int64_t>();
        if (seconds < 1 || seconds > MaximumBuildTimeout.count())
        {
            throw std::runtime_error(std::format("'timeoutSeconds' must be between 1 and {}", MaximumBuildTimeout.count()));
        }

        declared.command.timeout = std::chrono::seconds{ seconds };
    }

    auto const platformsIt = object.find("platforms");
    if (platformsIt != object.end() && !platformsIt->is_null())
    {
        if (!platformsIt->is_array())
        {
            throw std::runtime_error(std::format("'platforms' must be an array of platform names, not {}", platformsIt->dump()));
        }

        if (platformsIt->empty())
        {
            throw std::runtime_error("'platforms' must not be empty: omit it to select every platform");
        }

        for (auto const& element : *platformsIt)
        {
            if (!element.is_string())
            {
                throw std::runtime_error(std::format("'platforms' must be an array of platform names, not {}", element.dump()));
            }

            std::string const platform = element.get<std::string>();
            if (!IsKnownPlatform(platform))
            {
                throw std::runtime_error(std::format("Unknown platform '{}' in 'platforms': expected 'Windows', 'Linux', 'macOS' or 'all'", platform));
            }

            declared.platforms.push_back(platform);
        }
    }

    return declared;
}
} // namespace

BuildCommands ParseBuildConfiguration(std::string_view const jsonText, std::filesystem::path const& workspaceRoot, std::string_view const hostPlatform)
{
    json const root = json::parse(jsonText, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        throw std::runtime_error("Not a valid JSON object");
    }

    for (auto const& [key, value] : root.items())
    {
        if (key != "build")
        {
            throw std::runtime_error(std::format("Unknown top-level key '{}'", key));
        }
    }

    auto const buildIt = root.find("build");
    if (buildIt == root.end() || !buildIt->is_object())
    {
        throw std::runtime_error("'build' must be an object");
    }

    json const& build = *buildIt;

    for (auto const& [key, value] : build.items())
    {
        bool const known = std::ranges::any_of(KnownBuildKeys, [&key](char const* candidate)
        {
            return key == candidate;
        });
        if (!known)
        {
            throw std::runtime_error(std::format("Unknown key '{}' in 'build'", key));
        }
    }

    auto const commandsIt = build.find("commands");

    // Commands are collected exactly as declared, then filtered, so that the declaration order
    // survives the selection.
    std::vector<DeclaredBuildCommand> declared;

    if (commandsIt == build.end() || commandsIt->is_null())
    {
        declared.push_back(ParseBuildCommand(build, workspaceRoot));
    }
    else
    {
        if (build.size() != 1)
        {
            throw std::runtime_error(build.contains("command")
                                         ? "'build' must declare either 'command' or 'commands', not both"
                                         : "no key other than 'commands' may appear in 'build' when 'commands' is used");
        }

        if (!commandsIt->is_array())
        {
            throw std::runtime_error("'commands' must be an array of objects");
        }

        if (commandsIt->empty())
        {
            throw std::runtime_error("'commands' must not be empty");
        }

        for (auto const& element : *commandsIt)
        {
            if (!element.is_object())
            {
                throw std::runtime_error("'commands' must be an array of objects");
            }
            declared.push_back(ParseBuildCommand(element, workspaceRoot));
        }
    }

    BuildCommands selected;
    for (DeclaredBuildCommand const& command : declared)
    {
        if (IsSelectedForPlatform(command, hostPlatform))
        {
            selected.push_back(command.command);
        }
    }

    // The build tool exists whenever any command is declared, so a selection that leaves nothing
    // would leave it silently doing nothing: that is an invalid declaration, not an empty build.
    if (selected.empty())
    {
        throw std::runtime_error(std::format("The build declaration selects no command for platform '{}'", hostPlatform));
    }

    return selected;
}

std::optional<BuildCommands> LoadBuildConfiguration(std::filesystem::path const& workspaceRoot, std::string_view const hostPlatform)
{
    std::filesystem::path const path = workspaceRoot / BuildConfigurationFileName;

    std::error_code error;
    if (!std::filesystem::exists(path, error))
    {
        return std::nullopt;
    }

    try
    {
        return ParseBuildConfiguration(RawReadTextFile(path), workspaceRoot, hostPlatform);
    }
    catch (std::exception const& e)
    {
        throw std::runtime_error(std::format("Invalid build declaration {}: {}", path.string(), e.what()));
    }
}
