
#include "FileUtilities.h"
#include "NetworkUtilities.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using json = nlohmann::json;

namespace
{

constexpr ToolParameter ReadFileToolParameters[] =
{
    { "path"   , "The path to the file to read." },
};    

constexpr ToolParameter WriteFileToolParameters[] =
{
    { "path"   , "The path to the file to write."    },
    { "content", "The content to write to the file." },
};    

std::string ReadFileTool(json const& arguments)
{
    auto& path = arguments.at("path").get_ref<std::string const&>();
    return ReadTextFile(path);
}

std::string WriteFileTool(json const& arguments)
{
    auto& path    = arguments.at("path"   ).get_ref<std::string const&>();
    auto& content = arguments.at("content").get_ref<std::string const&>();

    WriteTextFile(path, content);
    return "[write_file] ok";
}

constexpr ToolDefinition AllTools[] =
{
    { "read_file" , "Read the contents of a file.", ReadFileToolParameters , ReadFileTool  },
    { "write_file", "Write content to a file."    , WriteFileToolParameters, WriteFileTool },
};

struct SessionRound
{
    std::string assistant;
    std::string user;
};

json BuildPayload(std::string_view const systemPrompt, std::string_view const initialUserPrompt, std::span<SessionRound> const conversationHistory = {})
{
    auto tools = json::array();
    for (auto&& toolDef : AllTools)
    {
        json toolJson = {
            { "name"       , toolDef.name        },
            { "description", toolDef.description },
        };

        if (!toolDef.parameters.empty())
        {
            json parametersJson = {
                { "type"      , "object" },
                { "properties", json::object() },
                { "required"  , json::array()  },
            };

            for (auto&& param : toolDef.parameters)
            {
                parametersJson["properties"][param.name] = {
                    { "type"       , "string"      },
                    { "description", param.description },
                };
                parametersJson["required"].push_back(param.name);
            }

            toolJson["parameters"] = std::move(parametersJson);
        }

        tools.push_back(
            json::object({
                { "type"    , "function" },
                { "function", std::move(toolJson) },
            })
        );
    }

    auto messages = json::array({
        { { "role", "system" }, { "content", systemPrompt } },
        { { "role", "user"   }, { "content", initialUserPrompt   } },
    });

    for (auto& round : conversationHistory)
    {
        messages.push_back({ { "role", "assistant" }, { "content", round.assistant } });
        messages.push_back({ { "role", "user"      }, { "content", round.user      } });
    }

    return json{
        { "model", "gemma-4" },
        { "messages", std::move(messages) },
        { "tools", std::move(tools) },
        //{ "max_tokens", 1024 },
        //{ "temperature", 0.7 },
        //{ "top_p", 1.0 },
        //{ "n", 1 },
        //{ "stream", false },
        //{ "stop", json::array({ "\n\n" }),
    };
}

enum class ResponseFinishReason
{
    stop          ,
    length        ,
    tool_calls    ,
    content_filter,
};

std::string to_string(ResponseFinishReason const reason)
{
    switch (reason)
    {
    case ResponseFinishReason::stop          : return "stop";
    case ResponseFinishReason::length        : return "length";
    case ResponseFinishReason::tool_calls    : return "tool_calls";
    case ResponseFinishReason::content_filter: return "content_filter";
    default                                  : return "<unknown ResponseFinishReason>";
    }
}

struct ToolCall
{
    std::string name;
    json        arguments;
};

struct ModelResponse
{
    ResponseFinishReason  reason;
    std::string           reasoning;
    std::string           content;
    std::vector<ToolCall> toolCalls;
};

ModelResponse ExtractModelContent(std::string_view const response_body)
{
    ModelResponse result;

    json const response = json::parse(response_body.begin(), response_body.end(), nullptr, false);
    if (!response.is_object()) throw std::runtime_error("malformed response: not a JSON object");

    json const& choices = response.at("choices");
    if (!choices.is_array() || choices.empty()) throw std::runtime_error("malformed response: missing choices");

    auto& choice = choices[0];

    auto& finishReason = choice.at("finish_reason").get_ref<std::string const&>();
    if      (finishReason == "stop"          ) result.reason = ResponseFinishReason::stop;
    else if (finishReason == "length"        ) result.reason = ResponseFinishReason::length;
    else if (finishReason == "tool_calls"    ) result.reason = ResponseFinishReason::tool_calls;
    else if (finishReason == "content_filter") result.reason = ResponseFinishReason::content_filter;
    else
    {
        throw std::runtime_error(std::string("malformed response: unknown finish_reason: ") + finishReason);
    }

    if (result.reason != ResponseFinishReason::stop)
    {
        std::cout << "Respponse: " << response_body << std::endl;
    }

    json const& message = choice.at("message");
    if (!message.is_object() || message.empty()) throw std::runtime_error("malformed response: missing message");

    result.content   = message.value<std::string>("content"          , {});
    result.reasoning = message.value<std::string>("reasoning_content", {});

    for (auto& toolCall : message.value<json::array_t>("tool_calls", {}))
    {
        if (toolCall.value<std::string>("type", {}) != "function")
        {
            continue;
        }
        auto& function         = toolCall.at("function");
        auto functionName      = function.value<std::string>("name", {});
        if (!functionName.empty())
        {
            result.toolCalls.push_back({
                .name      = std::move(functionName),
                .arguments = json::parse(function.value<std::string>("arguments", {}), nullptr, 0),
            });
        }
    }

    return result;
}

std::string HttpPost(Endpoint const& endpoint, json const& payload)
{
    return HttpPost(endpoint, "application/json", payload.dump());
}

}

int main(int argc, char** argv)
{
    std::string endpoint = "http://127.0.0.1:8080/v1/chat/completions";
    std::string prompt;
    int max_turns = 3;

    for (int i = 1; i < argc; ++i)
    {
        std::string const arg = argv[i];
        if (arg == "--endpoint" && i + 1 < argc)
        {
            endpoint = argv[++i];
        }
        else if (arg == "--max-turns" && i + 1 < argc)
        {
            max_turns = std::stoi(argv[++i]);
        }
        else if (prompt.empty())
        {
            prompt = arg;
        }
        else
        {
            prompt += " ";
            prompt += arg;
        }
    }

    if (prompt.empty())
    {
        prompt = "Read the README.md file and summarise it in one paragraph.";
    }

    Endpoint const endpointDescriptor = ParseEndpoint(endpoint);

    std::string const systemPrompt = ReadTextFile(GetExecutableDirectory() / "data" / "SystemPrompt.txt");
    std::string initialUserPrompt = prompt;

    std::vector<SessionRound> conversationHistory;

    for (int turn = 0; turn < max_turns; ++turn)
    {
        try
        {
            json const payload = BuildPayload(systemPrompt, initialUserPrompt, conversationHistory);
            //printf("Sending request to %s with payload:\n%s\n", endpoint.c_str(), payload.c_str());
            auto modelResponse = ExtractModelContent(HttpPost(endpointDescriptor, payload));

            // Show the reasoning stream from the model.
            if (!modelResponse.reasoning.empty())
            {
                std::cout << "reasoning> " << modelResponse.reasoning << std::endl << std::endl;
            }

            if (!modelResponse.content.empty())
            {
                // See if we got an old-school textual tool call.
                auto toolResponse = ParseToolCall(modelResponse.content, AllTools);
                if (toolResponse)
                {
                    auto& toolResult = toolResponse.value();

                    std::cout << "tool> " << OneLine(modelResponse.content, 20) << " -> " << OneLine(toolResult, 20) << std::endl << std::endl;

                    conversationHistory.push_back({
                        .assistant = std::move(modelResponse.content),
                        .user      = std::move(toolResult),
                    });
                    continue;
                }
                else
                {
                    std::cout << "assistant> " << modelResponse.content << std::endl << std::endl;
                }
            }

            if (modelResponse.reason == ResponseFinishReason::stop)
            {
                break;
            }
            else if (modelResponse.reason == ResponseFinishReason::tool_calls)
            {
                if (modelResponse.toolCalls.empty())
                {
                    std::cerr << "The model said it was calling tools, but we didn't find any tool to call" << std::endl;
                    break;
                }

                if (modelResponse.toolCalls.size() > 1)
                {
                    std::cerr << "TODO: multiple tool calls. Aborting..." << std::endl;
                    break;
                }

                auto& toolCall = modelResponse.toolCalls[0];
                std::string toolResult = CallTool(toolCall.name, toolCall.arguments, AllTools);
                std::cout << "tool> " << toolCall.name << '(' << OneLine(toolCall.arguments.dump(), 20) << ") -> " << OneLine(toolResult, 20) << std::endl << std::endl;
                conversationHistory.push_back({
                    .assistant = std::move(modelResponse.content),
                    .user      = std::move(toolResult),
                });
            }
            else
            {
                std::cerr << "TODO: stop reason " << to_string(modelResponse.reason);
                break;
            }
        }
        catch (std::exception const& error)
        {
            std::cerr << "agent error: " << error.what() << std::endl;
            return 1;
        }
    }

    return 0;
}
