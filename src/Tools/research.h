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
        Session session = context.createNewSession(RawReadTextFile(GetExecutableDirectory() / "data" / "ResearchSystemPrompt.txt"), Tools);

        response["response"] = session.Prompt(prompt, 50);
    }
    catch(const std::exception& e)
    {
        response["error"] = e.what();
    }
    
    return response;
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
