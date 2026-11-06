#pragma once

#include <BuildCommand.h>
#include <Process.h>
#include <ToolUtilities.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <format>
#include <string>

namespace
{

// The executable followed by each of its arguments, separated by single spaces, which is what
// the per-step header of a multi-command build reports.
std::string DescribeCommand(BuildCommand const& command)
{
    std::string description = command.executable;
    for (std::string const& argument : command.arguments)
    {
        description += ' ';
        description += argument;
    }

    return description;
}

// The combined output of several commands is capped at MaxProcessOutputBytes by keeping its
// first BuildOutputHeadBytes bytes and as many trailing bytes as the remaining budget allows.
constexpr std::size_t BuildOutputHeadBytes = 8 * 1024;
constexpr char        BuildOutputMarker[]  = "\n... output truncated ...\n";

std::string TruncateBuildOutput(std::string const& output, bool& truncated)
{
    if (output.size() <= MaxProcessOutputBytes)
    {
        return output;
    }

    std::size_t const headBytes = std::min(BuildOutputHeadBytes, output.size());
    std::size_t const tailBytes = std::min(MaxProcessOutputBytes - headBytes, output.size() - headBytes);

    truncated = true;

    return output.substr(0, headBytes) + BuildOutputMarker + output.substr(output.size() - tailBytes);
}

}

// Runs the build that the owners of the workspace declared. The commands, their arguments and
// their working directories all come from the workspace declaration, never from the model: this
// tool takes no arguments at all, so nothing the model writes can ever reach a command line.
// The declared commands run in order and the build stops at the first command that fails.
inline json BuildTool(json const& /*arguments*/, ToolsRuntimeContext const& context)
{
    if (!context.buildCommands.has_value())
    {
        return json{
            { "ok"        , false },
            { "status"    , "error" },
            { "errorCode" , "build_not_available" },
            { "summary"   , "This workspace does not declare a build, so there is nothing to build." },
        };
    }

    try
    {
        BuildCommands const& commands = *context.buildCommands;

        std::string output;
        bool        truncated     = false;
        bool        succeeded     = true;
        bool        timedOut      = false;
        int         exitCode      = 0;
        std::size_t failedCommand = 0;

        for (std::size_t index = 0; index < commands.size(); ++index)
        {
            BuildCommand const& command = commands[index];

            // With a single command there is nothing to disambiguate, so no header is written.
            if (commands.size() > 1)
            {
                if (!output.empty() && output.back() != '\n')
                {
                    output += '\n';
                }
                output += std::format("=== [{}/{}] {} ===\n", index + 1, commands.size(), DescribeCommand(command));
            }

            ProcessResult const result = RunProcess(command.executable, command.arguments, command.workingDirectory, command.timeout);

            output += result.output;
            truncated = truncated || result.truncated;

            if (result.timedOut || result.exitCode != 0)
            {
                succeeded     = false;
                timedOut      = result.timedOut;
                exitCode      = result.exitCode;
                failedCommand = index + 1;
                break;
            }

            exitCode = result.exitCode;
        }

        std::string const combinedOutput = TruncateBuildOutput(output, truncated);

        return json{
            { "ok"            , succeeded             },
            { "status"        , succeeded ? "succeeded" : (timedOut ? "timed_out" : "failed") },
            { "exitCode"      , exitCode              },
            { "timedOut"      , timedOut              },
            { "truncated"     , truncated             },
            { "commandCount"  , commands.size()       },
            { "failedCommand" , failedCommand         },
            { "output"        , combinedOutput        },
        };
    }
    catch (std::exception const& e)
    {
        return json{
            { "ok"        , false       },
            { "status"    , "error"     },
            { "errorCode" , "exception" },
            { "summary"   , e.what()    },
        };
    }
}

constexpr ToolDefinition build
{
    .name               = "build",
    .description        = "Run the build that the owners of this workspace declared, to check that your edits are correct. "
                          "The workspace may declare several commands, which run in order and stop at the first failure, and the output of all of them is combined. "
                          "This tool takes no arguments: the commands, their arguments, their working directories and their timeouts are all fixed by the workspace owners and cannot be changed or extended by you. "
                          "Returns the combined command output, the exit code, and whether the build timed out. "
                          "Call it once after a coherent set of edits, not after every single edit, and never call it again in a loop hoping for a different answer.",
    .callTool           = BuildTool,
};
