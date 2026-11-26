#pragma once

#include <ToolUtilities.h>

#include "subagent.h"

#include "glob.h"
#include "grep.h"
#include "read_file_chunk.h"
#include "edit_file_lines.h"
#include "write_file.h"
#include "build.h"
#include "delete_file.h"
#include "note.h"

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
    read_file_chunk,
    edit_file_lines,
    write_file,
    delete_file,
    note,
};

inline json ToolFunction(json const& arguments, ToolsRuntimeContext const& context, std::string_view const parentSessionId)
{
    // `build` is offered to the implementer only, and only when the owners of the workspace
    // have declared how to build it.
    std::vector<ToolDefinition> tools{ std::begin(Tools), std::end(Tools) };
    if (context.buildCommands.has_value())
    {
        tools.push_back(build);
    }

    return Tools::subagent::RunSubagent(context, parentSessionId, "implement", "ImplementSystemPrompt.txt", tools, arguments);
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
        "Resume this live subagent session instead of starting a new one: pass the `sessionId` an earlier implement "
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
    .name               = "implement",
    .description        = "Delegate an implementation task. "
                          "This tool may only perform reads and writes to files that you specifically mention. "
                          "The task can be high level or more mechanical. "
                          "For instance 'Refactor the body of the inner loop in function A of file F into its own function B', "
                          "'Rename the A member of class B from file F to C, including references in files G, H and I', "
                          "'Turn the global variable A into a parameter passed to all functions that need it transitively. Files: F, G, H', "
                          "etc... "
                          "When the task is complete, the tool will report the result. "
                          "The result reports `ok`, `sessionId`, `status`, `errorCode`, `stoppedAtTurnLimit`, `turns_used`, "
                          "`turns_allowed` and `summary`; when the subagent ran out of turns it still reports what it did, with "
                          "`status` \"stopped_at_turn_limit\" and `errorCode` \"too_many_turns\", so delegate only what remains, "
                          "and resume that `sessionId` with `turns` instead of starting over.",
    .requiredParameters = RequiredParameters,
    .optionalParameters = OptionalParameters,
    .callTool           = ToolFunction,
    .promptFragment     = "A subagent that ran out of turns has not lost its work: hand its `sessionId` back to "
                          "`implement` with `turns` to carry on in the same session, which still holds every file it "
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
// namespace Tools::implement
