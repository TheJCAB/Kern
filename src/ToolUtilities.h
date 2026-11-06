#pragma once

#include "BuildCommand.h"
#include "FileValidation.h"
#include "JsonUtilities.h"

#include <cstdint>
#include <functional>
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

// The turn budget handed to every subagent session started by `research` or `implement`.
constexpr unsigned SubagentMaxTurns = 50;

class Session;
struct ToolDefinition;
struct ToolsRuntimeContext;

// The context is passed in explicitly so that a factory never has to capture the
// context that owns it, which would leave a dangling reference inside every session.
using CreateNewSessionFunction = Session(ToolsRuntimeContext const& context, std::string_view systemPrompt, std::span<ToolDefinition const> tools);

struct ToolsRuntimeContext
{
    std::function<CreateNewSessionFunction> createNewSession;
    ValidatedFileSystem                     fs;

    // The build the workspace owners declared, if any. Tools that are only available when a
    // workspace declares a build must test this member before using it.
    std::optional<BuildCommands>            buildCommands;

    // Bare directory names that the search tools ignore below their root_dir, never at it.
    std::vector<std::string>                ignoredDirectories {};
};

// At runtime, running a tool is done by calling a function with this signature.
// - `arguments` is a JSON object sent by the model.
// - `ToolsRuntimeContext` provides data that it can use for its operation.
// - Returns a content string to send back to the model as response.
//   This can either be data for the model
//   or an error message if the parameters are found to be incorrect.
// It may throw an exception if the tool fails even though the call was correctly formed.
using ToolCallable = json(json const& arguments, ToolsRuntimeContext const&);

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
};

struct ToolCall
{
    std::string id;
    std::string name;
    json        arguments;
};

struct ToolCallDelta
{
    std::size_t index;
    std::string id;
    std::string name;
    std::string arguments;
};

json BuildPayloadToolDefinitions(std::span<ToolDefinition const>);

std::vector<ToolCall> ParseToolCalls(json::array_t const& toolCalls);

std::vector<ToolCallDelta> ParseToolCallsDelta(json::array_t const& toolCalls);

std::string CallTool(std::string_view const name, json const& arguments, ToolsRuntimeContext const&, std::span<ToolDefinition const>);

std::optional<std::string> ParseToolCall(std::string_view text, ToolsRuntimeContext const&, std::span<ToolDefinition const>);