#pragma once

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

using ToolCallable = std::string(json const& arguments);

struct ToolDefinition
{
    std::string_view               name;
    std::string_view               description;
    std::span<ToolParameter const> requiredParameters;
    std::span<ToolParameter const> optionalParameters;
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

std::string CallTool(std::string_view const name, json const& arguments, std::span<ToolDefinition const>);

std::optional<std::string> ParseToolCall(std::string_view text, std::span<ToolDefinition const>);
