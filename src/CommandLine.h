#pragma once

#include "Logging.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

// Thrown when the command line can't be understood. main() translates this into
// a diagnostic on stderr plus a non-zero exit code.
class UsageError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

enum class CommandLineAction
{
    run,
    printHelp,
    listServices,
    showSessions,
};

constexpr unsigned DefaultMaxTurns = 50;

struct CommandLineOptions
{
    CommandLineAction             action = CommandLineAction::run;
    std::string                   serviceName{};
    std::string                   endpointUrl{};
    std::optional<std::uint32_t>  port{};
    std::optional<std::string>    model{};
    std::optional<LogLevel>       logLevel{};
    unsigned                      maxTurns = DefaultMaxTurns;
    std::string                   prompt{};
    bool                          record = true;      // write a session manifest and turn file for every run; --record off disables it
    std::filesystem::path         sessionDirectory{}; // where transcripts go; empty means the logs/ directory beside the executable
    std::string                   sessionTag{};       // free-form label recorded in the manifest and used to select runs, e.g. tool-schema-v3
};

// Parses the arguments that follow the program name (i.e. argv[1..argc-1]).
// Throws UsageError for unknown options, missing option values, malformed
// numbers, duplicate/conflicting options, and for a missing prompt.
CommandLineOptions ParseCommandLine(std::span<std::string_view const> arguments);

// Writes the usage block to 'stream' (stdout by default; errors pass stderr).
void PrintUsage(std::FILE* stream = stdout);
