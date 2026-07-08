
#include "ToolUtilities.h"

#include <iostream>
#include <stdexcept>

std::optional<std::string> ParseToolCall(std::string_view const text, std::span<ToolDefinition const> const tools)
{
    json const toolCall = json::parse(text.begin(), text.end(), nullptr, false);
    if (!toolCall.is_object())
    {
        return {};
    }

    auto& tool = toolCall["tool"].get_ref<std::string const&>();

    for (auto&& toolDef : tools)
    {
        if (tool == toolDef.name)
        {
            try
            {
                return toolDef.callTool(toolCall);
            }
            catch (std::exception const e)
            {
                std::cerr << '[' << toolDef.name << "] " << e.what() << '\n';
                throw std::runtime_error{ e.what() };
            }
        }
    }

    return "[tool] unknown tool";
}
