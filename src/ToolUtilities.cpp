
#include "ToolUtilities.h"

#include <iostream>
#include <ranges>
#include <stdexcept>

namespace {
    template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; }; // (1)
    template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;  
}

json BuildPayloadToolDefinitions(std::span<ToolDefinition const> const tools)
{
    auto result = json::array();
    for (auto&& toolDef : tools)
    {
        json toolJson = {
            { "name"       , toolDef.name        },
            { "description", toolDef.description },
        };

        if (!toolDef.requiredParameters.empty() || !toolDef.optionalParameters.empty())
        {
            json parametersJson = {
                { "type"      , "object" },
                { "properties", json::object() },
                { "required"  , json::array()  },
            };

            auto& properties = parametersJson["properties"];
            auto& required   = parametersJson["required"];

            auto const addParameter = [&](ToolParameter const& param, bool const isRequired)
            {
                auto const addParameterBase =
                    [&](ToolParameterBase const& parameter, std::string_view const type) -> json&
                    {
                        if (isRequired) required.push_back(std::string(parameter.name));
                        return properties[parameter.name] = {
                            { "type"       , type                  },
                            { "description", parameter.description },
                        };
                    };

                std::visit(
                    overloaded{
                        [&](StringToolParameter const& parameter)
                        {
                            addParameterBase(parameter, "string");
                        },
                        [&](IntegerToolParameter const& parameter)
                        {
                            json& parameterJson = addParameterBase(parameter, "number");
                            if (parameter.minimum.has_value())
                            {
                                parameterJson["minimum"] = *parameter.minimum;
                            }
                            if (parameter.maximum.has_value())
                            {
                                parameterJson["maximum"] = *parameter.maximum;
                            }
                        },
                    },
                    param
                );
            };

            for (auto&& param : toolDef.requiredParameters)
            {
                addParameter(param, true);
            }

            for (auto&& param : toolDef.optionalParameters)
            {
                addParameter(param, false);
            }

            toolJson["parameters"] = std::move(parametersJson);
        }

        result.push_back(
            json::object({
                { "type"    , "function" },
                { "function", std::move(toolJson) },
            })
        );
    }
    return result;
}

std::vector<ToolCall> ParseToolCalls(json::array_t const& toolCalls)
{
    std::vector<ToolCall> result;
    for (auto& toolCall : toolCalls)
    {
        if (toolCall.value<std::string>("type", {}) != "function")
        {
            continue;
        }
        auto& function         = toolCall.at("function");
        auto functionName      = function.value<std::string>("name", {});
        if (!functionName.empty())
        {
            result.push_back({
                .id        = toolCall.value<std::string>("id", {}),
                .name      = std::move(functionName),
                .arguments = json::parse(function.value<std::string>("arguments", {}), nullptr, 0),
            });
        }
    }
    return result;
}

std::string CallTool(std::string_view const name, json const& arguments, ToolsRuntimeContext const& context, std::span<ToolDefinition const> const tools)
{
    auto const it = std::ranges::find_if(tools, [&](auto& toolDef){ return toolDef.name == name; });
    if (it == tools.end())
    {
        return "[tool] unknown tool";
    }

    auto& toolDef = *it;

    try
    {
        return toolDef.callTool(arguments, context);
    }
    catch (std::exception const e)
    {
        std::cerr << '[' << toolDef.name << "] " << e.what() << '\n';
        throw;
    }
}

std::optional<std::string> ParseToolCall(std::string_view const text, ToolsRuntimeContext const& context, std::span<ToolDefinition const> const tools)
{
    json const toolCall = json::parse(text.begin(), text.end(), nullptr, false);
    if (!toolCall.is_object())
    {
        return {};
    }

    return CallTool(toolCall.at("tool").get_ref<std::string const&>(), toolCall, context, tools);
}
