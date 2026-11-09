#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>
#include <Session.h>

#include "glob.h"
#include "grep.h"
#include "read_file_chunk.h"
#include "edit_file_lines.h"
#include "write_file.h"
#include "build.h"

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

        Session session = context.createNewSession(context, parentSessionId, "implement", RawReadTextFile(GetExecutableDirectory() / "data" / "ImplementSystemPrompt.txt"), tools);

        Session::PromptResult const result = session.Prompt(prompt, SubagentMaxTurns);

        return json{
            { "ok"        , result.ok                                },
            { "status"    , result.ok ? "complete" : "incomplete"    },
            { "errorCode" , result.errorCode                         },
            { "turns_used", result.turnsUsed                         },
            { "summary"   , result.content                           },
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
                          "When the task is complete, the tool will report the result.",
    .requiredParameters = RequiredParameters,
    .optionalParameters = {},
    .callTool           = ToolFunction
};

}