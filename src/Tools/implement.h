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

namespace Tools::implement
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
        Session session = context.createNewSession(RawReadTextFile(GetExecutableDirectory() / "data" / "ImplementSystemPrompt.txt"), Tools);

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
