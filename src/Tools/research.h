#pragma once

#include <ToolUtilities.h>

#include "subagent.h"

#include "glob.h"
#include "grep.h"
#include "read_file_chunk.h"
#include "build.h"
#include "note.h"

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
    note,
};

inline json ToolFunction(json const& arguments, ToolsRuntimeContext const& context, std::string_view const parentSessionId)
{
    // `build` is offered to the researcher only when the owners of the workspace have declared how to build it.
    // It is diagnostic only, because the researcher cannot edit files.
    std::vector<ToolDefinition> tools{ std::begin(Tools), std::end(Tools) };
    if (context.buildCommands.has_value())
    {
        tools.push_back(build);
    }

    return Tools::subagent::RunSubagent(context, parentSessionId, "research", "ResearchSystemPrompt.txt", tools, arguments);
}

constexpr ToolParameter RequiredParameters[] =
{
    StringToolParameter{ "prompt", "The **user** prompt that you provide to the subagent. This MUST include all necessary instructions and context specific to the task." }
};

constexpr ToolParameter OptionalParameters[] =
{
    StringToolParameter
    {
        "sessionId",
        "Resume this live subagent session instead of starting a new one: pass the `sessionId` an earlier research "
        "result reported. The subagent keeps its whole conversation, so send only the delta, and `turns` is granted "
        "on top of the budget it already has."
    },
    IntegerToolParameter
    {
        { "turns", "Turn budget for this call, granted on top of what a resumed session already holds. Defaults to "
                   "10; one session may never be granted more than 60 turns over its whole lifetime." },
        1, 60
    },
    BoolToolParameter
    {
        "release",
        "Release the subagent session named by `sessionId`, freeing its memory for good: it can never be resumed "
        "afterwards. Only 8 subagent sessions may be live at once, so release the ones you are done with. `prompt` "
        "is ignored when this is true."
    },
};

constexpr ToolDefinition Definition
{
    .name               = "research",
    .description        = "Delegate a research task to a research subagent. "
                          "This subagent may perform tasks like globbing, grepping, and reading files to gain knowledge of the workspace. "
                          "When the workspace declares a build, it may also run the fixed build command to report pre-existing build errors. "
                          "When the task is complete, the subagent will report the result. "
                          "The result reports `ok`, `sessionId`, `status`, `errorCode`, `stoppedAtTurnLimit`, `turns_used`, "
                          "`turns_allowed` and `summary`; when the subagent ran out of turns it still reports what it did, with "
                          "`status` \"stopped_at_turn_limit\" and `errorCode` \"too_many_turns\", so delegate only what remains, "
                          "and resume that `sessionId` with `turns` instead of starting over.",
    .requiredParameters = RequiredParameters,
    .optionalParameters = OptionalParameters,
    .callTool           = ToolFunction,
    .promptFragment     = "A subagent that ran out of turns has not lost its work: hand its `sessionId` back to "
                          "`research` with `turns` to carry on in the same session, which still holds every file it "
                          "read and every note it took, so send only what changed since. Restarting a stopped "
                          "subagent throws that away. Each live subagent session costs memory that is never freed "
                          "on its own, so at most 8 may be live: release the ones you have finished with by passing "
                          "their `sessionId` together with `release`. `turns` caps how many turns a session may "
                          "ever be granted over its whole lifetime and is no prediction of what it will use, so "
                          "grant what the task needs rather than the maximum: a session already granted 60 turns "
                          "in total refuses every further resume with `turn_cap` and must be replaced by a new one. "
                          "A `complete` session can be resumed as well, and because it still holds what it has "
                          "already read, resuming one is the cheapest way to ask a related follow-up question "
                          "about the same files, but `release` is irreversible and acts on complete sessions too, "
                          "so release only once no further question is possible. Ask one atomic question per "
                          "delegation and demand a bounded report of a line or two, with no restating of the "
                          "prompt and no recap of earlier findings, because a prompt that merely says please "
                          "answer produces long essays that cost context without adding information.",
};

}
// namespace Tools::research
