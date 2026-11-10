#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// Upper bound on the output returned to the caller, so that a runaway build can never
// flood the model's context.
constexpr std::size_t MaxProcessOutputBytes = 32 * 1024;

struct ProcessResult
{
    int         exitCode  = -1;    // -1 when the process was killed or did not exit normally
    bool        timedOut  = false;
    bool        truncated = false;
    std::string output;            // combined stdout and stderr
};

// Runs `executable` with `arguments` (which does not include the program name) directly,
// never through a shell, and captures the combined stdout and stderr.
// The program is looked up in PATH when `executable` is not a path.
// `workingDirectory` empty means: inherit the current directory.
// Throws std::runtime_error if the process cannot be started.
ProcessResult RunProcess(std::filesystem::path const& executable,
                         std::vector<std::string> const& arguments,
                         std::filesystem::path const& workingDirectory,
                         std::chrono::seconds timeout);
