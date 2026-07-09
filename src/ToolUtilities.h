#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>

using json = nlohmann::json;


struct ToolParameter
{
    std::string_view name;
    std::string_view description;
};

using ToolCall = std::string(json const& arguments);

struct ToolDefinition
{
    std::string_view               name;
    std::string_view               description;
    std::span<ToolParameter const> parameters;
    ToolCall&                      callTool;
};    

std::string CallTool(std::string_view const name, json const& arguments, std::span<ToolDefinition const> const tools);

std::optional<std::string> ParseToolCall(std::string_view const text, std::span<ToolDefinition const> const tools);
