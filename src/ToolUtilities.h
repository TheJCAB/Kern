#pragma once

#include "FileValidation.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using json = nlohmann::json;



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

using ToolParameter = std::variant<
    StringToolParameter,
    IntegerToolParameter>;

struct ToolsRuntimeContext
{
    ValidatedFileSystem fs;
};

// At runtime, running a tool is done by calling a function with this signature.
// - `arguments` is a JSON object sent by the model.
// - `ToolsRuntimeContext` provides data that it can use for its operation.
// - Returns a content string to send back to the model as response.
//   This can either be data for the model
//   or an error message if the parameters are found to be incorrect.
// It may throw an exception if the tool fails even though the call was correctly formed.
using ToolCallable = std::string(json const& arguments, ToolsRuntimeContext const&);

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

json BuildPayloadToolDefinitions(std::span<ToolDefinition const>);

std::vector<ToolCall> ParseToolCalls(json::array_t const& toolCalls);

std::string CallTool(std::string_view const name, json const& arguments, ToolsRuntimeContext const&, std::span<ToolDefinition const>);

std::optional<std::string> ParseToolCall(std::string_view text, ToolsRuntimeContext const&, std::span<ToolDefinition const>);
