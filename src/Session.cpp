#include "Session.h"

#include <Windows.h>

#include "FileUtilities.h"
#include "JsonUtilities.h"
#include "NetworkUtilities.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <print>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>


std::string HttpPost(Endpoint const& endpoint, json const& payload, std::function<void(std::string_view)> const& sseCallback)
{
    return HttpPost(endpoint, "application/json", payload.dump(), sseCallback);
}

struct Session::Pimpl
{
    static std::string GenerateLogId()
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

    struct Log
    {
        std::string id = GenerateLogId();
        int         sequence = 0;
    };

    static void WriteLogFile(Log& log, std::string_view const name, std::string_view const content)
    {
        auto const logPath = BuildLogPath(log.id, log.sequence, name);
        if (auto output = std::ofstream(logPath, std::ios::binary | std::ios::trunc))
        {
            output << content;
        }
        else
        {
            throw std::runtime_error(std::string("unable to write log file: ") + logPath.string());
        }
    }

    struct ConversationMessage
    {
        std::string   role      {};
        std::string   content   {};
        std::string   toolCallId{};
        json::array_t toolCalls {};
    };

    json BuildPayload()
    {
        json::array_t messages{};

        for (auto& message : m_conversationHistory)
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
            { "model", m_modelName },
            { "messages", std::move(messages) },
            { "stream", !IsOllamaEndpoint() },
            { "tools", BuildPayloadToolDefinitions(m_tools) },
            //{ "max_tokens", 1024 },
            //{ "temperature", 0.7 },
            //{ "top_p", 1.0 },
            //{ "n", 1 },
            //{ "stop", json::array({ "\n\n" }),
        };
    }

    enum class ResponseFinishReason
    {
        notFinished   ,
        stop          ,
        length        ,
        tool_calls    ,
        content_filter,
    };

    friend std::string to_string(ResponseFinishReason const reason)
    {
        switch (reason)
        {
        case ResponseFinishReason::notFinished   : return "notFinished";
        case ResponseFinishReason::stop          : return "stop";
        case ResponseFinishReason::length        : return "length";
        case ResponseFinishReason::tool_calls    : return "tool_calls";
        case ResponseFinishReason::content_filter: return "content_filter";
        default                                  : return "<unknown ResponseFinishReason>";
        }
    }

    struct ModelResponse
    {
        ResponseFinishReason  reason;
        std::string           reasoning;
        std::string           content;
        std::vector<ToolCall> toolCalls;
    };

    struct ModelResponseDelta
    {
        ResponseFinishReason       reason;
        std::string                reasoning;
        std::string                content;
        std::vector<ToolCallDelta> toolCalls;
    };

    bool IsOllamaEndpoint()
    {
        return m_endpointDescriptor.path.find("/api/chat") != std::string::npos || m_endpointDescriptor.path.find("/api/generate") != std::string::npos;
    }

    ModelResponseDelta ExtractModelContentDelta(json const& event)
    {
        ModelResponseDelta result;

        if (!event.is_object()) throw std::runtime_error("response SSE event: not a JSON object");

        if (!event.contains("choices")) throw std::runtime_error("malformed SSE event: missing choices");

        json const& choices = event.at("choices");
        if (!choices.is_array()) throw std::runtime_error("malformed SSE event choices: not an array");
        if (choices.empty()) throw std::runtime_error("malformed SSE event choices: it's empty");

        auto& choice = choices[0];

        auto const finishReason = GetOrDefault(choice, "finish_reason", "notFinished");
        if      (finishReason == "stop"          ) result.reason = ResponseFinishReason::stop;
        else if (finishReason == "length"        ) result.reason = ResponseFinishReason::length;
        else if (finishReason == "tool_calls"    ) result.reason = ResponseFinishReason::tool_calls;
        else if (finishReason == "content_filter") result.reason = ResponseFinishReason::content_filter;
        else if (finishReason == "notFinished"   ) result.reason = ResponseFinishReason::notFinished;
        else
        {
            throw std::runtime_error(std::string("malformed SSE event: unknown finish_reason: ") + finishReason);
        }

        //if (result.reason != ResponseFinishReason::notFinished)
        //{
        //    std::cout << "Response SSE event:\n" << event.dump(2) << std::endl << std::endl;
        //}

        json const& delta = choice.at("delta");
        result.content   = GetOrDefault(delta, "content"          , "");
        result.reasoning = GetOrDefault(delta, "reasoning_content", GetOrDefault(delta, "thinking", ""));
        result.toolCalls = ParseToolCallsDelta(GetOrDefault<json::array_t>(delta, "tool_calls", {}));

        return result;
    }

    ModelResponse ExtractModelContent(json const& response)
    {
        ModelResponse result;

        if (!response.is_object()) throw std::runtime_error("malformed response: not a JSON object");

        if (IsOllamaEndpoint())
        {
            auto const& message = response.value("message", json::object());
            if (!message.is_object()) throw std::runtime_error("malformed response: missing message");

            result.content = message.value<std::string>("content", {});

            if (response.value("done", false))
            {
                auto const doneReason = response.value("done_reason", std::string{"stop"});
                if      (doneReason == "stop"      ) result.reason = ResponseFinishReason::stop;
                else if (doneReason == "length"   ) result.reason = ResponseFinishReason::length;
                else if (doneReason == "tool_calls") result.reason = ResponseFinishReason::tool_calls;
                else result.reason = ResponseFinishReason::stop;
            }
            else
            {
                result.reason = ResponseFinishReason::length;
            }

            if (auto const toolCalls = message.value<json::array_t>("tool_calls", {}); !toolCalls.empty())
            {
                result.toolCalls = ParseToolCalls(toolCalls);
                if (result.reason == ResponseFinishReason::stop)
                {
                    result.reason = ResponseFinishReason::tool_calls;
                }
            }

            return result;
        }

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

        if (result.reason != ResponseFinishReason::stop && result.reason != ResponseFinishReason::tool_calls)
        {
            std::cout << "Response:\n" << response.dump(2) << std::endl << std::endl;
        }

        json const& message = choice.at("message");
        if (!message.is_object() || message.empty()) throw std::runtime_error("malformed response: missing message");

        result.content   = message.value<std::string>("content"          , {});
        result.reasoning = message.value<std::string>("reasoning_content", message.value<std::string>("thinking", {}));
        result.toolCalls = ParseToolCalls(message.value<json::array_t>("tool_calls", {}));

        return result;
    }

    static std::string BuildToolResponseMessage(std::string_view const toolName, std::string_view const toolCallId, std::string_view const toolResult)
    {
        json response = {
            { "result", std::string(toolResult) },
        };

        if (!toolName.empty())
        {
            response["tool"] = std::string(toolName);
        }

        if (!toolCallId.empty())
        {
            response["tool_call_id"] = std::string(toolCallId);
        }

        return response.dump();
    }

    static std::filesystem::path const& GetLogsDirectory()
    {
        static auto const path = GetExecutableDirectory() / "logs";
        if (!std::filesystem::exists(path))
        {
            std::filesystem::create_directory(path);
        }
        return path;
    }

    static std::filesystem::path BuildLogPath(std::string_view const logId, int sequence, std::string_view const name)
    {
        std::ostringstream filename;
        filename << logId << '_' << std::setw(3) << std::setfill('0') << sequence << '_' << name << ".json";
        return GetLogsDirectory() / filename.str();
    }

    Pimpl(Config config)
        : m_endpointDescriptor{ std::move(config.endpointDescriptor)     }
        , m_toolContext       { std::move(config.toolContext)            }
        , m_modelName         { std::move(config.modelName)              }
        , m_tools             { config.tools.begin(), config.tools.end() }
    {
        m_conversationHistory.push_back({ .role = "system", .content = std::move(config.systemPrompt) });

        std::cout << "log id: " << m_log.id << std::endl;
    }

    std::string Prompt(std::string_view prompt, int maxTurns)
    {
        if (prompt.empty())
        {
            return {};
        }

        std::println("===============================\n[{}]\nPrompt: {}", m_log.id, prompt);

        m_conversationHistory.push_back({ .role{ "user" }, .content{ prompt } });

        for (int turn = 0; turn < maxTurns; ++turn, ++m_log.sequence)
        {
            std::println("===============================\nTurn {} [{}]", turn, m_log.id);
            try
            {
                json const payload = BuildPayload();
                WriteLogFile(m_log, "request", payload.dump(2));

                ModelResponse modelResponse;
                if (IsOllamaEndpoint())
                {
                    std::string const responseBody = HttpPost(m_endpointDescriptor, payload, [](auto){});
                    //std::cout << "Response: " << responseBody << std::endl;

                    json const response = json::parse(responseBody.begin(), responseBody.end(), nullptr, false);
                    WriteLogFile(m_log, "response", response.dump(2));

                    modelResponse = ExtractModelContent(response);
                }
                else
                {
                    json response;

                    std::map<std::size_t, ToolCallDelta> toolCalls;

                    auto sseCallback =
                        [&](std::string_view event)
                        {
                            OutputDebugString(std::format("Response SSE Event: {}\n", event).c_str());
                            json jsonEvent = json::parse(event.begin(), event.end(), nullptr, false);
                            auto modelResponseDelta = ExtractModelContentDelta(jsonEvent);
                            if (!modelResponseDelta.reasoning.empty())
                            {
                                if (modelResponse.reasoning.empty())
                                {
                                    std::cout << "\nreasoning> ";
                                }
                                std::cout << modelResponseDelta.reasoning;
                                modelResponse.reasoning += modelResponseDelta.reasoning;
                            }
                            if (!modelResponseDelta.content.empty())
                            {
                                if (modelResponse.content.empty())
                                {
                                    std::cout << "\nassistant> ";
                                }
                                std::cout << modelResponseDelta.content;
                                modelResponse.content += modelResponseDelta.content;
                            }
                            if (modelResponseDelta.reason != ResponseFinishReason::notFinished)
                            {
                                modelResponse.reason = modelResponseDelta.reason;
                            }
                            for (auto&& toolCallDelta : modelResponseDelta.toolCalls)
                            {
                                auto& toolCall = toolCalls[toolCallDelta.index];
                                toolCall.id        += toolCallDelta.id;
                                toolCall.name      += toolCallDelta.name;
                                toolCall.arguments += toolCallDelta.arguments;
                            }
                        };

                    (void)HttpPost(m_endpointDescriptor, payload, sseCallback);

                    if (!modelResponse.reasoning.empty())
                    {
                        std::cout << modelResponse.reasoning << std::endl << std::endl;
                    }

                    for (auto&& [index, call] : toolCalls)
                    {
                        std::print("Assembled tool call: {}({})\n", call.name, call.arguments);
                        modelResponse.toolCalls.push_back(
                            ToolCall{
                                .id        = call.id,
                                .name      = call.name,
                                .arguments = json::parse(call.arguments.begin(), call.arguments.end(), nullptr, false),
                            }
                        );
                    }

                    json assembledResponse{
                        { "reason"   , to_string(modelResponse.reason) },
                        { "reasoning", modelResponse.reasoning         },
                        { "content"  , modelResponse.content           },
                    };

                    json::array_t tool_calls;
                    for (auto&& call : modelResponse.toolCalls)
                    {
                        tool_calls.push_back(json{
                            { "id"       , call.id },
                            { "name"     , call.name },
                            { "arguments", call.arguments },
                        });
                    }

                    assembledResponse["tool_calls"] = std::move(tool_calls);

                    WriteLogFile(m_log, "response", assembledResponse.dump(2));
                }

                // Show the reasoning stream from the model.
                if (!modelResponse.reasoning.empty() && IsOllamaEndpoint())
                {
                    std::cout << "\nreasoning> " << modelResponse.reasoning << std::endl << std::endl;
                }

                if (!modelResponse.content.empty())
                {
                    // See if we got an old-school textual tool call.
                    auto toolResponse = ParseToolCall(modelResponse.content, m_toolContext, m_tools);
                    if (toolResponse)
                    {
                        auto& toolResult = toolResponse.value();

                        std::cout << "tool> " << OneLine(modelResponse.content, 20) << " -> " << OneLine(toolResult, 20) << std::endl << std::endl;

                        m_conversationHistory.push_back({
                            .role    = "user",
                            .content = toolResult,
                        });
                        m_conversationHistory.push_back({
                            .role    = "assistant",
                            .content = std::move(modelResponse.content),
                        });
                        continue;
                    }
                    else if (IsOllamaEndpoint()) // Otherwise, we already did during streaming
                    {
                        std::cout << "\nassistant> " << modelResponse.content << std::endl << std::endl;
                    }
                }

                if (modelResponse.reason == ResponseFinishReason::stop)
                {
                    m_conversationHistory.push_back({
                        .role      = "assistant",
                        .content   = modelResponse.content,
                    });
                    return std::move(modelResponse.content);
                }
                else if (modelResponse.reason == ResponseFinishReason::tool_calls)
                {
                    if (modelResponse.toolCalls.empty())
                    {
                        if (modelResponse.content.empty())
                        {
                            std::cerr << "The model said it was calling tools, but we didn't find any tool to call" << std::endl;
                            throw std::runtime_error("The model said it was calling tools, but we didn't find any tool to call");
                        }
                        else
                        {
                            // Figure it's really a stop...?
                            m_conversationHistory.push_back({
                                .role      = "assistant",
                                .content   = modelResponse.content,
                            });
                            return std::move(modelResponse.content);
                        }
                    }
                    if (!IsOllamaEndpoint())
                    {
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
                        m_conversationHistory.push_back(std::move(assistantMessage));
                    }

                    for (auto const& toolCall : modelResponse.toolCalls)
                    {
                        std::print("\ntool call [{}]> {}({})\n", toolCall.id, toolCall.name, OneLine(toolCall.arguments.dump(), 60));
                        std::string toolResult = CallTool(toolCall.name, toolCall.arguments, m_toolContext, m_tools);
                        std::print("\ntool result [{}]> {} -> {}\n", toolCall.id, toolCall.name, OneLine(toolResult, 60));
                        if (IsOllamaEndpoint())
                        {
                            //m_conversationHistory.push_back({
                            //    .role      = "assistant",
                            //    .content   = json{
                            //        { "type", "function" },
                            //        { "function", {
                            //            { "name"     , toolCall.name             },
                            //            { "arguments", toolCall.arguments.dump() },
                            //        }},
                            //        { "id", toolCall.id },
                            //    }.dump(),
                            //});
                            m_conversationHistory.push_back({
                                .role       = "user",
                                .content    = BuildToolResponseMessage(toolCall.name, toolCall.id, toolResult),
                            });
                        }
                        else
                        {
                            m_conversationHistory.push_back({
                                .role       = "tool",
                                .content    = BuildToolResponseMessage(toolCall.name, toolCall.id, toolResult),
                                .toolCallId = toolCall.id,
                            });
                        }
                    }
                }
                else
                {
                    std::cerr << "TODO: stop reason " << to_string(modelResponse.reason);
                    throw std::runtime_error(std::string("TODO: stop reason ") + to_string(modelResponse.reason));
                }
            }
            catch (std::exception const& error)
            {
                std::cerr << "agent error: " << error.what() << std::endl;
                throw;
            }
        }

        return "The model took too many turns";
    }


    Endpoint                         m_endpointDescriptor;
    ToolsRuntimeContext              m_toolContext;
    std::string                      m_modelName;
    std::vector<ToolDefinition>      m_tools;

    std::vector<ConversationMessage> m_conversationHistory;
    Log                              m_log;
};

Session::Session(Config config) : m_pimpl{ std::make_unique<Pimpl>(std::move(config)) } {}

Session::~Session() = default;

std::string Session::Prompt(std::string_view prompt, int maxTurns)
{
    return m_pimpl->Prompt(prompt, maxTurns);
}
