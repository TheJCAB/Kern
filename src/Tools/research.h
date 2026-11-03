#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>
#include <Session.h>

#include "glob.h"
#include "grep.h"
#include "read_file_chunk.h"

#include <string>
#include <string_view>

namespace Tools::research
{

constexpr ToolDefinition Tools[] =
{
    glob,
    grep,
    read_file_chunk,
};

inline json ToolFunction(json const& arguments, ToolsRuntimeContext const& context)
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
        Session session = context.createNewSession(context, RawReadTextFile(GetExecutableDirectory() / "data" / "ResearchSystemPrompt.txt"), Tools);

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
                          "When the task is complete, the subagent will report the result.",
    .requiredParameters = RequiredParameters,
    .optionalParameters = {},
    .callTool           = ToolFunction
};

}
// namespace Tools::research