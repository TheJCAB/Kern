#pragma once

#include <FileUtilities.h>
#include "RuntimePaths.h"
#include <ToolUtilities.h>
#include <Session.h>

#include "glob.h"
#include "grep.h"
#include "read_file_chunk.h"
#include "edit_file_lines.h"
#include "write_file.h"
#include "build.h"
#include "memory.h"

#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace Tools::implement
{

// The tools that the implementer may always use. `build` is added on top of this list only
// when the workspace declares a build (see ToolFunction below); this tool is never handed to
// the orchestrator or to any other subagent.
constexpr ToolDefinition Tools[] =
{
    //read_file,
    //glob,
    //grep,
    read_file_chunk,
    edit_file_lines,
    write_file,
    memory,
};

inline json ToolFunction(json const& arguments, ToolsRuntimeContext const& context, std::string_view const parentSessionId)
{
    std::string const prompt = arguments.value("prompt", "");

    if (prompt.empty())
    {
        return json{
            { "ok"        , false                     },
            { "status"    , "error"                   },
            { "errorCode" , "empty_prompt"            },
            { "turns_used", 0u                        },
            { "summary"   , "prompt must not be empty" },
        };
    }

    try
    {
        // `build` is offered to the implementer only, and only when the owners of the workspace
        // have declared how to build it.
        std::vector<ToolDefinition> tools{ std::begin(Tools), std::end(Tools) };
        if (context.buildCommands.has_value())
        {
            tools.push_back(build);
        }

        auto systemPrompt = RawReadTextFile(GetRuntimeDataDirectory() / "ImplementSystemPrompt.txt");
        systemPrompt += std::format("\nYou have a total of {} turns to complete your task.", SubagentMaxTurns);

        Session session = context.createNewSession(context, parentSessionId, "implement", systemPrompt, tools, SubagentMaxTurns);

        Session::PromptResult const result = session.Prompt(prompt);

        // A wrap-up report, granted once the subagent spent its turn budget, is still an answer,
        // but it is not a finished task, so it must not read as "complete".
        std::string status = "incomplete";
        if (result.ok)
        {
            status = result.stoppedAtTurnLimit ? "stopped_at_turn_limit" : "complete";
        }

        return json{
            { "ok"                 , result.ok                             },
            { "status"             , std::move(status)                     },
            { "errorCode"          , result.errorCode                      },
            { "stoppedAtTurnLimit" , result.stoppedAtTurnLimit             },
            { "turns_used"         , result.turnsUsed                      },
            { "summary"            , result.content                        },
        };
    }
    catch (std::exception const& e)
    {
        return json{
            { "ok"        , false        },
            { "status"    , "error"      },
            { "errorCode" , "exception"  },
            { "turns_used", 0u           },
            { "summary"   , e.what()     },
        };
    }
}

constexpr ToolParameter RequiredParameters[] =
{
    StringToolParameter{ "prompt", "The **user** prompt that you provide to the subagent. This MUST include all necessary instructions and context specific to the task." }
};

constexpr ToolDefinition Definition
{
    .name               = "implement",
    .description        = "Delegate an implementation task. "
                          "This tool may only perform reads and writes to files that you specifically mention. "
                          "The task can be high level or more mechanical. "
                          "For instance 'Refactor the body of the inner loop in function A of file F into its own function B', "
                          "'Rename the A member of class B from file F to C, including references in files G, H and I', "
                          "'Turn the global variable A into a parameter passed to all functions that need it transitively. Files: F, G, H', "
                          "etc... "
                          "When the task is complete, the tool will report the result. "
                          "The result reports `ok`, `status`, `errorCode`, `stoppedAtTurnLimit`, `turns_used` and `summary`; when the "
                          "subagent ran out of turns it still reports what it did, with `status` \"stopped_at_turn_limit\" and "
                          "`errorCode` \"too_many_turns\", so delegate only what remains. ",
    .requiredParameters = RequiredParameters,
    .optionalParameters = {},
    .callTool           = ToolFunction
};

}
// namespace Tools::implement
