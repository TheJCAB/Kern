#include "Session.h"

#include "FileUtilities.h"
#include "JsonUtilities.h"
#include "Logging.h"
#include "NetworkUtilities.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"
#include "XmlToolCallParser.h"

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


HttpResult HttpPost(Endpoint const& endpoint, json const& payload, std::function<void(std::string_view)> const& sseCallback)
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

    struct LogFileState
    {
        std::string id = GenerateLogId();
        int         sequence = 0;
    };

    static void WriteLogFile(LogFileState& log, std::string_view const name, std::string_view const content, std::string_view const extension = "json")
    {
        auto const logPath = BuildLogPath(log.id, log.sequence, name, extension);
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
            { "tools", BuildPayloadToolDefinitions(m_tools) },
            { "stream", !IsOllamaEndpoint() },
            //{ "max_tokens", 1024 },
            //{ "temperature", 0.7 },
            //{ "top_p", 1.0 },
            //{ "n", 1 },
            { "messages", std::move(messages) },
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
        default                                  : return std::format("<unknown ResponseFinishReason {}>", static_cast<int>(reason));
        }
    }

    struct ModelResponse
    {
        ResponseFinishReason  reason = ResponseFinishReason::notFinished;
        std::string           reasoning;
        std::string           content;
        std::vector<ToolCall> toolCalls;
    };

    struct ModelResponseDelta
    {
        ResponseFinishReason       reason = ResponseFinishReason::notFinished;
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
        if (choices.empty())
        {
            // Legitimate: services send a final usage-only event (stream_options.include_usage) with no choices.
            if (event.contains("usage"))
            {
                Log(LogLevel::debug, "token usage: {}", event.at("usage").dump());
            }
            else
            {
                Log(LogLevel::debug, "ignoring SSE event with no choices: {}", event.dump());
            }
            return result;
        }

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
            std::println("Response:\n{}\n", response.dump(2));
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

    static std::filesystem::path BuildLogPath(std::string_view const logId, int sequence, std::string_view const name, std::string_view const extension)
    {
        std::ostringstream filename;
        filename << logId << '_' << std::setw(3) << std::setfill('0') << sequence << '_' << name << '.' << extension;
        return GetLogsDirectory() / filename.str();
    }

    static void CheckHttpResult(HttpResult const& result)
    {
        if (!result.error.empty())
        {
            throw std::runtime_error("HTTP request failed: " + result.error);
        }
        if (!result.IsSuccess())
        {
            constexpr std::size_t maxBodyInMessage = 1000;
            std::string body = result.body.substr(0, maxBodyInMessage);
            if (result.body.size() > maxBodyInMessage)
            {
                body += "...";
            }
            throw std::runtime_error(std::format("the service answered HTTP {}: {}", result.status, body));
        }
    }

    Pimpl(Config config)
        : m_endpointDescriptor{ std::move(config.endpointDescriptor)     }
        , m_toolContext       { std::move(config.toolContext)            }
        , m_modelName         { std::move(config.modelName)              }
        , m_tools             { config.tools.begin(), config.tools.end() }
    {
        m_conversationHistory.push_back({ .role = "system", .content = std::move(config.systemPrompt) });

        std::println("log id: {}", m_log.id);
    }

    std::string Prompt(std::string_view prompt, unsigned maxTurns)
    {
        if (prompt.empty())
        {
            return {};
        }

        std::println("===============================\n[{}]\nPrompt: {}", m_log.id, prompt);

        m_conversationHistory.push_back({ .role{ "user" }, .content{ prompt } });

        for (unsigned turn = 0; turn < maxTurns; ++turn, ++m_log.sequence)
        {
            std::println("===============================\nTurn {} [{}]", turn, m_log.id);
            try
            {
                json const payload = BuildPayload();
                WriteLogFile(m_log, "request", payload.dump(2));
                Log(LogLevel::debug, "turn {}: sending {} messages to model '{}' (request saved as log {}_{:03}_request.json)",
                    turn, m_conversationHistory.size(), m_modelName, m_log.id, m_log.sequence);

                ModelResponse modelResponse;
                if (IsOllamaEndpoint())
                {
                    HttpResult const httpResult = HttpPost(m_endpointDescriptor, payload, [](auto){});
                    if (IsLogEnabled(LogLevel::debug))
                    {
                        WriteLogFile(m_log, "response_raw", httpResult.body, "txt");
                    }
                    CheckHttpResult(httpResult);
                    std::string const& responseBody = httpResult.body;
                    //std::cout << "Response: " << responseBody << std::endl;

                    json const response = json::parse(responseBody.begin(), responseBody.end(), nullptr, false);
                    WriteLogFile(m_log, "response", response.dump(2));

                    modelResponse = ExtractModelContent(response);
                    Log(LogLevel::debug, "finish_reason: {}, content: {} chars, tool calls: {}",
                        to_string(modelResponse.reason), modelResponse.content.size(), modelResponse.toolCalls.size());
                }
                else
                {
                    json response;

                    std::map<std::size_t, ToolCallDelta> toolCalls;

                    auto sseCallback =
                        [&](std::string_view event)
                        {
                            //std::println("Response SSE Event: {}\n", event);
                            json jsonEvent = json::parse(event.begin(), event.end(), nullptr, false);
                            if (jsonEvent.is_discarded())
                            {
                                Log(LogLevel::error, "SSE event is not valid JSON: {}", event.substr(0, 500));
                            }
                            auto modelResponseDelta = ExtractModelContentDelta(jsonEvent);
                            if (!modelResponseDelta.reasoning.empty())
                            {
                                if (modelResponse.content.empty())
                                {
                                    if (modelResponse.reasoning.empty())
                                    {
                                        std::print("\nreasoning>\n\n  | ");
                                    }
                                    auto reasoning = modelResponseDelta.reasoning;
                                    ReplaceNewlinesIn(reasoning, "\n  | ");
                                    std::print("{}", reasoning);
                                }
                                modelResponse.reasoning += modelResponseDelta.reasoning;
                            }
                            if (!modelResponseDelta.content.empty())
                            {
                                if (modelResponse.content.empty())
                                {
                                    std::println("\nassistant>\n");
                                }
                                std::print("{}", modelResponseDelta.content);
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
                            std::cout.flush();
                        };

                    HttpResult const httpResult = HttpPost(m_endpointDescriptor, payload, sseCallback);
                    if (IsLogEnabled(LogLevel::debug))
                    {
                        WriteLogFile(m_log, "response_raw", httpResult.body, "txt");
                    }
                    CheckHttpResult(httpResult);

                    std::println();

                    if (modelResponse.reason == ResponseFinishReason::notFinished)
                    {
                        // Typically this means the body wasn't an event stream at all, or the stream was cut short.
                        Log(LogLevel::error, "the response stream ended without a finish_reason (HTTP {}, {} bytes, content: {} chars, tool calls: {})",
                            httpResult.status, httpResult.body.size(), modelResponse.content.size(), toolCalls.size());
                        Log(LogLevel::error, "response body starts with: {}", httpResult.body.substr(0, 500));
                        throw std::runtime_error("the response ended without a finish_reason; run with --log-level debug (or trace) to inspect it");
                    }
                    Log(LogLevel::debug, "finish_reason: {}, content: {} chars, reasoning: {} chars, tool calls: {}",
                        to_string(modelResponse.reason), modelResponse.content.size(), modelResponse.reasoning.size(), toolCalls.size());

                    for (auto&& [index, call] : toolCalls)
                    {
                        std::println("Assembled tool call: {}({})", call.name, call.arguments);
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
                    std::println("\nreasoning> {}\n", modelResponse.reasoning);
                }

                if (!modelResponse.content.empty())
                {
                    // See if we got an old-school textual tool call.
                    auto toolResponse = ParseToolCall(modelResponse.content, m_toolContext, m_tools);
                    if (toolResponse)
                    {
                        auto& toolResult = toolResponse.value();

                        std::println("tool> {} -> {}\n", OneLine(modelResponse.content, 20), OneLine(toolResult, 20));

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
                        std::println("\nassistant> {}\n", modelResponse.content);
                    }
                }

                if (modelResponse.reason == ResponseFinishReason::stop)
                {
                    if (modelResponse.content.empty() && modelResponse.toolCalls.empty())
                    {
                        modelResponse.toolCalls = ParseToolCallsXml(modelResponse.reasoning);
                    }
                    if (!modelResponse.toolCalls.empty())
                    {
                        modelResponse.reason = ResponseFinishReason::tool_calls;
                        std::println("Found {} XML tools", modelResponse.toolCalls.size());
                    }
                    else
                    {
                        m_conversationHistory.push_back({
                            .role      = "assistant",
                            .content   = modelResponse.content,
                        });
                        return std::move(modelResponse.content);
                    }
                }

                if (modelResponse.reason == ResponseFinishReason::tool_calls)
                {
                    if (modelResponse.toolCalls.empty())
                    {
                        if (modelResponse.content.empty())
                        {
                            std::println("The model said it was calling tools, but we didn't find any tool to call");
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
                        std::println("\ntool call [{}]> {}({})", toolCall.id, toolCall.name, OneLine(toolCall.arguments.dump(), 60));
                        std::string toolResult = CallTool(toolCall.name, toolCall.arguments, m_toolContext, m_tools);
                        std::println("\ntool result [{}]> {} -> {}", toolCall.id, toolCall.name, OneLine(toolResult, 60));
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
                    std::println("TODO: stop reason {}", to_string(modelResponse.reason));
                    throw std::runtime_error(std::string("TODO: stop reason ") + to_string(modelResponse.reason));
                }
            }
            catch (std::exception const& error)
            {
                std::println("agent error: {}", error.what());
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
    LogFileState                     m_log;
};

Session::Session(Config config) : m_pimpl{ std::make_unique<Pimpl>(std::move(config)) } {}

Session::~Session() = default;

Session::Session(Session&&) = default;

Session& Session::operator=(Session&&) = default;

std::string Session::Prompt(std::string_view prompt, unsigned maxTurns)
{
    return m_pimpl->Prompt(prompt, maxTurns);
}