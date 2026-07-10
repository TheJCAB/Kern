
#include "FileUtilities.h"
#include "NetworkUtilities.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <span>
#include <sstream>
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

struct ConversationMessage
{
    std::string   role;
    std::string   content;
    std::string   toolCallId;
    json::array_t toolCalls;
};

json BuildPayload(std::string_view const systemPrompt, std::string_view const initialUserPrompt, std::span<ConversationMessage> const conversationHistory = {})
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

    for (auto& message : conversationHistory)
    {
        json messageJson = {
            { "role"   , message.role    },
            { "content", message.content },
        };

        if (!message.toolCallId.empty())
        {
            messageJson["tool_call_id"] = message.toolCallId;
        }

        if (!message.toolCalls.empty())
        {
            messageJson["tool_calls"] = message.toolCalls;
        }

        messages.push_back(std::move(messageJson));
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
    std::string id;
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

ModelResponse ExtractModelContent(json const& response)
{
    ModelResponse result;

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
        std::cout << "Response: " << response.dump(2) << std::endl;
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
                .id        = toolCall.value<std::string>("id", {}),
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

std::string BuildToolResponseMessage(std::string_view const toolName, std::string_view const toolResult)
{
    json response = {
        { "result", std::string(toolResult) },
    };

    if (!toolName.empty())
    {
        response["tool"] = std::string(toolName);
    }

    return response.dump();
}

std::string GenerateLogId()
{
    static constexpr char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";

    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::uniform_int_distribution<int> dist(0, static_cast<int>(sizeof(kAlphabet) - 2));

    std::string id;
    id.reserve(8);
    for (int i = 0; i < 8; ++i)
    {
        id.push_back(kAlphabet[dist(rng)]);
    }

    return id;
}

std::filesystem::path GetLogsDirectory()
{
    static auto const path = GetExecutableDirectory() / "logs";
    if (!std::filesystem::exists(path))
    {
        std::filesystem::create_directory(path);
    }
    return path;
}

std::filesystem::path BuildLogPath(std::filesystem::path const& logsDirectory, std::string_view const logId, int sequence, std::string_view const prefix)
{
    std::ostringstream filename;
    filename << logId << '_' << std::setw(3) << std::setfill('0') << sequence << '_' << prefix << ".json";
    return logsDirectory / filename.str();
}

void WriteLogFile(std::filesystem::path const& logsDirectory, std::string_view const logId, int sequence, std::string_view const prefix, std::string_view const content)
{
    auto const logPath = BuildLogPath(logsDirectory, logId, sequence, prefix);
    std::ofstream output(logPath, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error(std::string("unable to write log file: ") + logPath.string());
    }

    output << content;
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

    std::string const logId = GenerateLogId();
    std::filesystem::path const logsDirectory = GetLogsDirectory();
    std::cout << "log id: " << logId << std::endl;

    std::vector<ConversationMessage> conversationHistory;

    for (int turn = 0; turn < max_turns; ++turn)
    {
        std::cout << std::endl << "===============================\nTurn " << turn << std::endl << std::endl;
        try
        {
            json const payload = BuildPayload(systemPrompt, initialUserPrompt, conversationHistory);
            WriteLogFile(logsDirectory, logId, turn, "request", payload.dump(2));

            std::string const responseBody = HttpPost(endpointDescriptor, payload);

            json const response = json::parse(responseBody.begin(), responseBody.end(), nullptr, false);
            WriteLogFile(logsDirectory, logId, turn, "response", response.dump(2));

            auto modelResponse = ExtractModelContent(response);

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
                        .role    = "user",
                        .content = toolResult,
                    });
                    conversationHistory.push_back({
                        .role    = "assistant",
                        .content = std::move(modelResponse.content),
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

                ConversationMessage assistantMessage{
                    .role      = "assistant",
                    .content   = std::move(modelResponse.content),
                    .toolCalls = json::array(),
                };
                for (auto const& toolCall : modelResponse.toolCalls)
                {
                    assistantMessage.toolCalls.push_back({
                        { "type", "function" },
                        { "function", {
                            { "name"     , toolCall.name             },
                            { "arguments", toolCall.arguments.dump() },
                        }},
                        { "id", toolCall.id },
                    });
                }
                conversationHistory.push_back(std::move(assistantMessage));

                for (auto const& toolCall : modelResponse.toolCalls)
                {
                    std::string toolResult = CallTool(toolCall.name, toolCall.arguments, AllTools);
                    std::cout << "tool> " << toolCall.name << '(' << OneLine(toolCall.arguments.dump(), 20) << ") -> " << OneLine(toolResult, 20) << std::endl << std::endl;
                    conversationHistory.push_back({
                        .role        = "tool",
                        .content     = BuildToolResponseMessage(toolCall.name, toolResult),
                        .toolCallId  = toolCall.id,
                    });
                }
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
