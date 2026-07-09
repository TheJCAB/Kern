
#include "ToolUtilities.h"

#include <iostream>
#include <ranges>
#include <stdexcept>

std::string CallTool(std::string_view const name, json const& arguments, std::span<ToolDefinition const> const tools)
{
    auto const it = std::ranges::find_if(tools, [&](auto& toolDef){ return toolDef.name == name; });
    if (it == tools.end())
    {
        return "[tool] unknown tool";
    }

    auto& toolDef = *it;

    try
    {
        return toolDef.callTool(arguments);
    }
    catch (std::exception const e)
    {
        std::cerr << '[' << toolDef.name << "] " << e.what() << '\n';
        throw;
    }
}

std::optional<std::string> ParseToolCall(std::string_view const text, std::span<ToolDefinition const> const tools)
{
    json const toolCall = json::parse(text.begin(), text.end(), nullptr, false);
    if (!toolCall.is_object())
    {
        return {};
    }

    return CallTool(toolCall.at("tool").get_ref<std::string const&>(), toolCall, tools);
}
