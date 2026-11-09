#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>
#include <Session.h>

#include "glob.h"
#include "grep.h"
#include "read_file_chunk.h"
#include "build.h"

#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace Tools::research
{
// The tools that the researcher may always use. `build` is added on top of this list only when
// the workspace declares a build (see ToolFunction below); the orchestrator never gets `build`.
constexpr ToolDefinition Tools[] =
{
    glob,
    grep,
    read_file_chunk,
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
        // `build` is offered to the researcher only when the owners of the workspace have declared
        // how to build it. It is diagnostic only, because the researcher cannot edit files.
        std::vector<ToolDefinition> tools{ std::begin(Tools), std::end(Tools) };
        if (context.buildCommands.has_value())
        {
            tools.push_back(build);
        }

        Session session = context.createNewSession(context, parentSessionId, "research", RawReadTextFile(GetExecutableDirectory() / "data" / "ResearchSystemPrompt.txt"), tools);

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
    .name               = "research",
    .description        = "Delegate a research task to a research subagent. "
                          "This subagent may perform tasks like globbing, grepping, and reading files to gain knowledge about the workspace. "
                          "When the workspace declares a build, it may also run the fixed build command to report pre-existing build errors. "
                          "When the task is complete, the subagent will report the result.",
    .requiredParameters = RequiredParameters,
    .optionalParameters = {},
    .callTool           = ToolFunction
};

}
// namespace Tools::research