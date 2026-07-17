#pragma once

#include <FileUtilities.h>
#include <ToolUtilities.h>
#include <Session.h>

#include "glob.h"
#include "grep.h"
#include "read_file_chunk.h"
#include "edit_file_lines.h"
#include "write_file.h"

#include <string>
#include <string_view>

namespace
{

constexpr ToolDefinition Tools[] =
{
    //read_file,
    //glob,
    //grep,
    read_file_chunk,
    edit_file_lines,
    write_file,
};

inline json SubagentTool(json const& arguments, ToolsRuntimeContext const& context)
{
    std::string const prompt = arguments.value("prompt", "");
    
    json response{
        { "prompt", prompt },
    };

    if (prompt.empty())
    {
        response["error"] = "prompt must not be empty";
        return response;
    }

    try
    {
        Session session = context.createNewSession(RawReadTextFile(GetExecutableDirectory() / "data" / "SystemPrompt.txt"), Tools);

        response["response"] = session.Prompt(prompt, 50);
    }
    catch(const std::exception& e)
    {
        response["error"] = e.what();
    }
    
    return response;
}

constexpr ToolParameter SubagentToolParameters[] =
{
    StringToolParameter{ "prompt", "The prompt to provide to the subagent. This should include all necessary instructions and context for the task." }
};

constexpr ToolDefinition subagent
{
    .name               = "subagent",
    .description        = "Delegate a task to a subagent. The subagent will perform the menial work and return the result.",
    .requiredParameters = SubagentToolParameters,
    .optionalParameters = {},
    .callTool           = SubagentTool
};
}
