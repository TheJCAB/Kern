#pragma once

#include "BuildCommand.h"
#include "FileValidation.h"
#include "JsonUtilities.h"
#include "SubagentSessions.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>



struct ToolParameterBase
{
    std::string_view name;
    std::string_view description;
};

struct StringToolParameter : ToolParameterBase
{
};

struct IntegerToolParameter : ToolParameterBase
{
    std::optional<std::int64_t> minimum{};
    std::optional<std::int64_t> maximum{};
};

struct BoolToolParameter : ToolParameterBase
{
};

struct StringArrayToolParameter : ToolParameterBase
{
};

using ToolParameter = std::variant<StringToolParameter, IntegerToolParameter, BoolToolParameter, StringArrayToolParameter>;

struct ToolDefinition;
struct ToolsRuntimeContext;

// Opens a subagent session, or resumes the live one named by `resumeSessionId`: the very same session, with the
// same conversation history and the same record, granted `maxTurns` more turns on top of the budget it already
// has. `role`, `systemPrompt` and `tools` only reach a session this call starts, because a resumed session keeps
// its own. The context is passed in explicitly so that a factory never has to capture the context that owns it,
// which would leave a dangling reference inside every session.
using CreateNewSessionFunction = std::expected<SubagentRunResult, SubagentError>(
    ToolsRuntimeContext const&      ,
    std::string_view                parentSessionId,
    std::string_view                role,
    std::string_view                systemPrompt,
    std::span<ToolDefinition const> ,
    unsigned                        maxTurns,
    std::optional<std::string_view> resumeSessionId,
    std::string_view                prompt
);

// Releases live subagent sessions and returns how many went away: the one named by `sessionId`, or every live one
// when the id is empty. Each live session holds a conversation that never shrinks, so releasing is the only way
// to give that memory back before the main session ends.
using ReleaseSubagentSessionsFunction = std::size_t(std::string_view sessionId);

struct ToolsRuntimeContext
{
    std::function<CreateNewSessionFunction>        createNewSession;
    std::function<ReleaseSubagentSessionsFunction> releaseSubagentSessions;
    ValidatedFileSystem                            fs;

    // The build the workspace owners declared, if any. Tools that are only available when a
    // workspace declares a build must test this member before using it.
    std::optional<BuildCommands>                   buildCommands;

    // Bare directory names that the search tools ignore below their root_dir, never at it.
    std::vector<std::string>                       ignoredDirectories{};

    // The turn of the calling session that the current tool calls belong to, set by the session before it runs a
    // turn's tools. It is what lets a subagent tool refuse two resumes of one subagent session in the same turn.
    unsigned                                       turnEpoch = 0;
};

// At runtime, running a tool is done by calling a function with this signature.
// - `arguments` is a JSON object sent by the model.
// - `ToolsRuntimeContext` provides data that it can use for its operation.
// - Returns a content string to send back to the model as response.
//   This can either be data for the model
//   or an error message if the parameters are found to be incorrect.
// It may throw an exception if the tool fails even though the call was correctly formed.
using ToolCallable = json(json const& arguments, ToolsRuntimeContext const&, std::string_view const parentSessionId);

// A tool argument was present but unusable; tools report this as errorCode "invalid_argument".
struct ToolArgumentError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// Reads the optional 'exclude' argument: a JSON array of bare directory names.
// A missing or null argument yields an empty vector. A non-array value, a non-string entry,
// or an entry containing '/' or '\\' or equal to "." or ".." throws ToolArgumentError naming the entry.
std::vector<std::string> ParseExcludeArgument(json const& arguments);

struct ToolDefinition
{
    std::string_view               name;
    std::string_view               description;
    std::span<ToolParameter const> requiredParameters{};
    std::span<ToolParameter const> optionalParameters{};
    ToolCallable&                  callTool;
    // Standing policy about this tool's place in the workflow, appended to the system prompt when the tool
    // is active; call mechanics belong in description instead, and an empty value contributes nothing.
    std::string_view               promptFragment{};
};

struct ToolCall
{
    std::string id;
    std::string name;
    json        arguments;

    [[nodiscard]] friend bool operator==(ToolCall const& a, ToolCall const& b)
    {
        if (!a.id.empty() && !b.id.empty())
        {
            return a.id == b.id;
        }
        return a.name == b.name && a.arguments == b.arguments;
    }
};

struct ToolCallDelta
{
    std::size_t index;
    std::string id;
    std::string name;
    std::string arguments;
};

json BuildPayloadToolDefinitions(std::span<ToolDefinition const>);

// Joins the prompt fragment of every tool that has one, in tool order, so the session prompt can be
// assembled once when the session starts. Returns an empty string when no tool has a fragment.
std::string BuildToolPromptFragments(std::span<ToolDefinition const> tools);

std::vector<ToolCall> ParseToolCalls(json::array_t const& toolCalls);

std::vector<ToolCallDelta> ParseToolCallsDelta(json::array_t const& toolCalls);

std::string CallTool(std::string_view const name, json const& arguments, ToolsRuntimeContext const&, std::string_view const parentSessionId, std::span<ToolDefinition const>);

std::optional<std::string> ParseToolCall(std::string_view text, ToolsRuntimeContext const&, std::string_view const parentSessionId, std::span<ToolDefinition const>);