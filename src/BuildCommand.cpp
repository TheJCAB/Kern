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

constexpr char const* KnownBuildCommandKeys[] = { "command", "arguments", "workingDirectory", "timeoutSeconds" };

constexpr char const* KnownBuildKeys[] = { "command", "arguments", "workingDirectory", "timeoutSeconds", "commands" };

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

bool IsWithin(std::filesystem::path const& root, std::filesystem::path const& candidate)
{
    std::filesystem::path const rootPath      = std::filesystem::absolute(root).lexically_normal();
    std::filesystem::path const candidatePath = std::filesystem::absolute(candidate).lexically_normal();

    auto const mismatch = std::mismatch(rootPath.begin(), rootPath.end(), candidatePath.begin(), candidatePath.end());
    return mismatch.first == rootPath.end();
}

// Parses and validates one fully resolved command out of a build declaration object.
BuildCommand ParseBuildCommand(json const& object, std::filesystem::path const& workspaceRoot)
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

    BuildCommand command;

    command.executable = ReadRequiredString(object, "command");

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
            command.arguments.push_back(element.get<std::string>());
        }
    }

    auto const directoryIt = object.find("workingDirectory");
    if (directoryIt != object.end() && !directoryIt->is_null())
    {
        if (!directoryIt->is_string())
        {
            throw std::runtime_error("'workingDirectory' must be a string");
        }

        std::filesystem::path const declared = directoryIt->get<std::string>();
        command.workingDirectory = (declared.is_absolute() ? declared : workspaceRoot / declared).lexically_normal();
        command.workingDirectory = std::filesystem::absolute(command.workingDirectory).lexically_normal();

        if (!IsWithin(workspaceRoot, command.workingDirectory))
        {
            throw std::runtime_error("'workingDirectory' must be inside the workspace");
        }

        std::error_code error;
        if (!std::filesystem::is_directory(command.workingDirectory, error))
        {
            throw std::runtime_error(std::format("'workingDirectory' is not a directory: {}", command.workingDirectory.string()));
        }
    }
    else
    {
        command.workingDirectory = std::filesystem::absolute(workspaceRoot).lexically_normal();
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

        command.timeout = std::chrono::seconds{ seconds };
    }

    return command;
}
} // namespace

BuildCommands ParseBuildConfiguration(std::string_view const jsonText, std::filesystem::path const& workspaceRoot)
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
    if (commandsIt == build.end() || commandsIt->is_null())
    {
        return BuildCommands{ ParseBuildCommand(build, workspaceRoot) };
    }

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

    BuildCommands commands;
    for (auto const& element : *commandsIt)
    {
        if (!element.is_object())
        {
            throw std::runtime_error("'commands' must be an array of objects");
        }
        commands.push_back(ParseBuildCommand(element, workspaceRoot));
    }

    return commands;
}

std::optional<BuildCommands> LoadBuildConfiguration(std::filesystem::path const& workspaceRoot)
{
    std::filesystem::path const path = workspaceRoot / BuildConfigurationFileName;

    std::error_code error;
    if (!std::filesystem::exists(path, error))
    {
        return std::nullopt;
    }

    try
    {
        return ParseBuildConfiguration(RawReadTextFile(path), workspaceRoot);
    }
    catch (std::exception const& e)
    {
        throw std::runtime_error(std::format("Invalid build declaration {}: {}", path.string(), e.what()));
    }
}
