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
#include <thread>
#include <chrono>
#include <type_traits>
#include <vector>


HttpResult HttpPost(Endpoint const& endpoint, json const& payload, std::function<void(std::string_view)> const& sseCallback)
{
    // Rate limits (429) and temporary unavailability (503) are transient; non-2xx bodies carry no SSE events,
    // so repeating the request can't duplicate any output already handed to the callback.
    constexpr int maxAttempts = 6;
    std::string const body = payload.dump();
    for (int attempt = 1;; ++attempt)
    {
        HttpResult result = HttpPost(endpoint, "application/json", body, sseCallback);
        if ((result.status != 429 && result.status != 503) || attempt >= maxAttempts)
        {
            return result;
        }

        int const backoffSeconds = std::min(5 << (attempt - 1), 60); // 5, 10, 20, 40, 60
        int const waitSeconds = std::clamp(result.retryAfterSeconds, backoffSeconds, 120);
        Log(LogLevel::warning, "the service answered HTTP {} (attempt {} of {}); retrying in {} s. Response: {}",
            result.status, attempt, maxAttempts, waitSeconds, result.body.substr(0, 300));
        std::this_thread::sleep_for(std::chrono::seconds(waitSeconds));
    }
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

    json BuildPayload(std::vector<ConversationMessage> const& transientMessages = {})
    {
        json::array_t messages{};

        auto appendMessage = [&](ConversationMessage const& message)
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
        };

        for (auto const& message : m_conversationHistory)
        {
            appendMessage(message);
        }
        for (auto const& message : transientMessages)
        {
            appendMessage(message);
        }

        json result{
            { "model", m_modelName },
            { "tools", BuildPayloadToolDefinitions(m_tools) },
            { "stream", !IsOllamaEndpoint() },
            //{ "temperature", 0.7 },
            //{ "top_p", 1.0 },
            //{ "n", 1 },
            { "messages", std::move(messages) },
        };
        if (m_maxTokens != 0)
        {
            result["max_tokens"] = m_maxTokens;
        }
        return result;
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

    static std::string BuildToolResponseMessage(std::string_view const toolResult)
    {
        return std::string(toolResult);
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

    // Builds the recorder identity: an empty id comes from the log id, an empty role means "main".
    static SessionIdentity MakeRecorderIdentity(Config const& config, std::string_view const logId)
    {
        SessionIdentity identity = config.identity;
        if (identity.id.empty())
        {
            identity.id = std::string(logId);
        }
        if (identity.role.empty())
        {
            identity.role = "main";
        }
        return identity;
    }

    // Builds the provenance row; credential material is never recorded, so apiKey is not copied.
    static SessionProvenance MakeRecorderProvenance(Config const& config)
    {
        SessionProvenance provenance;
        provenance.modelName = config.modelName;
        provenance.maxTokens = config.maxTokens;
        provenance.endpoint  = json{
            { "host"  , config.endpointDescriptor.host   },
            { "port"  , config.endpointDescriptor.port   },
            { "path"  , config.endpointDescriptor.path   },
            { "secure", config.endpointDescriptor.secure },
        };
        return provenance;
    }

    // Builds the inputs row; the user prompt is left empty because BeginSession fills it in.
    static SessionInputs MakeRecorderInputs(Config const& config)
    {
        SessionInputs inputs;
        inputs.systemPrompt    = config.systemPrompt;
        inputs.toolDefinitions = BuildPayloadToolDefinitions(config.tools);
        return inputs;
    }

    // Picks the directory that receives the manifest: the configured one, or the logs directory.
    static std::filesystem::path MakeRecorderDirectory(Config const& config)
    {
        return config.sessionDirectory.empty() ? std::filesystem::path(GetLogsDirectory()) : config.sessionDirectory;
    }

    Pimpl(Config config)
        : m_endpointDescriptor{ config.endpointDescriptor               }
        , m_toolContext       { std::move(config.toolContext)            }
        , m_modelName         { config.modelName                         }
        , m_tools             { config.tools.begin(), config.tools.end() }
        , m_maxTokens         { config.maxTokens                         }
        , m_log               {}
        , m_recorder          { MakeRecorderIdentity(config, m_log.id),
                                MakeRecorderProvenance(config),
                                MakeRecorderInputs(config),
                                MakeRecorderDirectory(config),
                                config.record                        }
    {
        std::string systemPrompt = std::move(config.systemPrompt);
        if (m_maxTokens != 0)
        {
            // Reasoning and the reply share one output budget, so say so up front and not only after a cut-off.
            systemPrompt += "\n\nYour output budget for each response is " + std::to_string(m_maxTokens)
                          + " tokens, shared between your reasoning and your reply, so a long chain of thought can use all of it"
                            " and leave no room for the tool call or the answer you were thinking about.";
        }
        m_conversationHistory.push_back({ .role = "system", .content = std::move(systemPrompt) });

        std::println("log id: {}", m_log.id);

        // The decoding parameters actually sent; BuildPayload hard-codes streaming and only sends max_tokens when
        // non-zero.
        m_recorder.SetDecoding(json{ { "stream", !IsOllamaEndpoint() }, { "maxTokens", m_maxTokens } });
    }

    // Writes exactly one line to the recorder's jsonl file per loop iteration from its destructor, so that a
    // return, a continue and a throw each produce exactly one line.
    class TurnRecord
    {
    public:
        TurnRecord(SessionRecorder& recorder, unsigned turn)
            : m_recorder{ recorder }
        {
            m_turn = json{ { "turn", turn } };
        }

        ~TurnRecord() noexcept
        {
            if (!m_recorder.Enabled())
            {
                return;
            }
            SessionRecorder::NormaliseTokenUsage(m_turn);
            m_recorder.RecordTurn(m_turn);
        }

        void SetUsage(json const& usage)
        {
            if (!usage.is_object())
            {
                return;
            }
            m_turn["usage"] = usage;
        }

        void SetResponseMilliseconds(double milliseconds)
        {
            m_turn["responseMs"] = milliseconds;
        }

        void SetFinishReason(std::string_view reason)
        {
            if (reason.empty())
            {
                return;
            }
            m_turn["finishReason"] = reason;
        }

        void SetAssistantText(std::string_view text)
        {
            if (!text.empty())
            {
                m_turn["assistantText"] = text;
            }
            else if (m_turn.contains("finishReason"))
            {
                m_turn["assistantText"] = "";
            }
        }

        void AddToolCall(json const& call)
        {
            if (!m_turn.contains("toolCalls"))
            {
                m_turn["toolCalls"] = json::array();
            }
            m_turn["toolCalls"].push_back(call);
        }

        void AddToolResult(std::string_view name, std::string const& result)
        {
            if (!m_turn.contains("toolResults"))
            {
                m_turn["toolResults"] = json::array();
            }
            json& entry = m_turn["toolResults"].emplace_back(json{ { "tool", name } });
            // The full result stays in the conversation; the turn line keeps a truncated copy.
            try
            {
                json const parsed = json::parse(result);
                if (parsed.is_object() && parsed.contains("error"))
                {
                    entry["error"] = parsed.at("error").is_string() ? parsed.at("error").get<std::string>() : parsed.at("error").dump();
                }
                else
                {
                    entry["result"] = parsed;
                }
            }
            catch (json::exception const&)
            {
                entry["result"] = result;
            }
            entry["resultLength"] = result.size();
            if (entry.contains("result") && entry["result"].is_string() && entry["result"].get_ref<std::string const&>().size() > 2000)
            {
                entry["result"].get_ref<std::string&>().resize(2000);
                entry["resultTruncated"] = true;
            }
        }

    private:
        SessionRecorder& m_recorder;
        json             m_turn;
    };

    PromptResult RunPrompt(std::string_view prompt, unsigned maxTurns)
    {
        if (prompt.empty())
        {
            return { .ok = false, .errorCode = "empty_prompt", .content = {}, .turnsUsed = 0 };
        }

        std::println("===============================\n[{}]\nPrompt: {}", m_log.id, prompt);

        m_conversationHistory.push_back({ .role{ "user" }, .content{ prompt } });

        // Holds the cut-off output plus a "hurry up" nudge for exactly one request; never stored in the history.
        // The first retry explains the situation, the second is a bare order to act, and a third cut-off gives up.
        std::vector<ConversationMessage> hurryMessages;
        unsigned hurryRetries = 0;
        constexpr unsigned maxHurryRetries = 2;
        for (unsigned turn = 0; turn < maxTurns; ++turn, ++m_log.sequence)
        {
            TurnRecord turnRecord{ m_recorder, turn };
            json accumulatedUsage = json::object();
            std::println("===============================\nTurn {} [{}]", turn, m_log.id);
            try
            {
                json const payload = BuildPayload(hurryMessages);
                WriteLogFile(m_log, "request", payload.dump(2));
                Log(LogLevel::debug, "turn {}: sending {} messages to model '{}' (request saved as log {}_{:03}_request.json)",
                    turn, m_conversationHistory.size(), m_modelName, m_log.id, m_log.sequence);

                ModelResponse modelResponse;
                if (IsOllamaEndpoint())
                {
                    auto const requestStart = std::chrono::steady_clock::now();
                    HttpResult const httpResult = HttpPost(m_endpointDescriptor, payload, [](auto){});
                    turnRecord.SetResponseMilliseconds(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - requestStart).count());
                    if (IsLogEnabled(LogLevel::debug))
                    {
                        WriteLogFile(m_log, "response_raw", httpResult.body, "txt");
                    }
                    CheckHttpResult(httpResult);
                    std::string const& responseBody = httpResult.body;
                    //std::cout << "Response: " << responseBody << std::endl;

                    json const response = json::parse(responseBody.begin(), responseBody.end(), nullptr, false);
                    if (response.contains("usage"))
                    {
                        turnRecord.SetUsage(response.at("usage"));
                    }
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
                            if (jsonEvent.contains("usage") && jsonEvent.at("usage").is_object())
                            {
                                accumulatedUsage = jsonEvent.at("usage");
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

                    auto const requestStart = std::chrono::steady_clock::now();
                    HttpResult const httpResult = HttpPost(m_endpointDescriptor, payload, sseCallback);
                    turnRecord.SetResponseMilliseconds(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - requestStart).count());
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

                if (accumulatedUsage.is_object() && !accumulatedUsage.empty())
                {
                    turnRecord.SetUsage(accumulatedUsage);
                }
                turnRecord.SetFinishReason(to_string(modelResponse.reason));
                turnRecord.SetAssistantText(modelResponse.content);

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
                        return {
                            .ok        = true,
                            .errorCode = {},
                            .content   = std::move(modelResponse.content),
                            .turnsUsed = turn + 1,
                        };
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
                            return {
                                .ok        = true,
                                .errorCode = {},
                                .content   = std::move(modelResponse.content),
                                .turnsUsed = turn + 1,
                            };
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
                        turnRecord.AddToolCall(json{ { "id", toolCall.id }, { "name", toolCall.name }, { "arguments", toolCall.arguments } });
                        std::string toolResult = CallTool(toolCall.name, toolCall.arguments, m_toolContext, m_tools);
                        turnRecord.AddToolResult(toolCall.name, toolResult);
                        std::println("\ntool result [{}]> {} -> {}", toolCall.id, toolCall.name, OneLine(toolResult, 60));
                        if (IsOllamaEndpoint())
                        {
                            m_conversationHistory.push_back({
                                .role       = "tool",
                                .content    = BuildToolResponseMessage(toolResult),
                                .toolCallId = toolCall.id,
                            });
                        }
                        else
                        {
                            m_conversationHistory.push_back({
                                .role       = "tool",
                                .content    = BuildToolResponseMessage(toolResult),
                                .toolCallId = toolCall.id,
                            });
                        }
                    }
                    // A turn that produced work resets the cut-off escalation to the polite first retry.
                    hurryRetries = 0;
                    hurryMessages.clear();
                }
                else if (modelResponse.reason == ResponseFinishReason::length && hurryRetries < maxHurryRetries)
                {
                    // Token cap hit mid-thought: for one request only, show the model its partial output and tell it to act.
                    // The replacement response is what ends up in the history.
                    ++hurryRetries;
                    std::println("Response hit the token limit after {} characters of reasoning and {} characters of content; asking the model to wrap up (retry {}/{})",
                        modelResponse.reasoning.size(), modelResponse.content.size(), hurryRetries, maxHurryRetries);
                    std::string partial;
                    if (!modelResponse.reasoning.empty())
                    {
                        partial += "[Your reasoning so far, cut off by the output token limit. It is carried over so that you do not have to spend your budget repeating it, and it is settled: do not restate, revise or re-derive any of it.]\n"
                                + modelResponse.reasoning
                                + "\n[End of earlier reasoning]\n";
                    }
                    if (!modelResponse.content.empty())
                    {
                        partial += "[Partial reply]\n" + modelResponse.content + "\n[End of partial reply]\n";
                    }
                    if (partial.empty())
                    {
                        partial = "(there was no output before the token limit was reached)";
                    }
                    hurryMessages.push_back({
                        .role    = "assistant",
                        .content = std::move(partial),
                    });

                    std::string const budget = m_maxTokens == 0 ? std::string("a fixed number of tokens") : std::to_string(m_maxTokens) + " tokens";
                    std::string nudge;
                    if (hurryRetries == 1)
                    {
                        // First retry: explain the situation, name the budget and make the failure mode explicit.
                        nudge = "Your previous response was cut off because it reached the output token limit before it produced any tool call or answer. "
                                "This is a continuation of the same task, not its completion. The whole budget, " + budget + ", applies again to this response, "
                                "and the reasoning above was carried over so that you do not have to spend tokens repeating it. "
                                "Treat everything above as settled: do not restate, revise or re-derive it. "
                                "Spend a short amount of thought here, a paragraph and not a plan, and then act: make the next tool call now. "
                                "If the whole task does not fit, do the first part completely and report the rest. "
                                "A response that contains only reasoning is a failure: the retry is spent, the session ends and the work above is lost.";
                    }
                    else
                    {
                        // Second retry: no more analysis, one action.
                        nudge = "Your previous response was cut off again without producing a tool call or an answer. "
                                "Stop analysing and respond now, with appropriate tool calls or just a final response, from the plan you already settled on above; "
                                "if that plan is incomplete, call the tool with what you have. Keep any reasoning to a single short paragraph. "
                                "This is the last retry: another reasoning-only response ends the session and discards all of the work above.";
                    }
                    hurryMessages.push_back({
                        .role    = "user",
                        .content = std::move(nudge),
                    });
                }
                else if (modelResponse.reason == ResponseFinishReason::length)
                {
                    std::println("Response hit the token limit again without producing a tool call or an answer; giving up");
                    return {
                        .ok        = false,
                        .errorCode = "length",
                        .content   = "The model hit the token limit repeatedly without producing a tool call or an answer",
                        .turnsUsed = turn + 1,
                    };
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

        return  { .ok = false, .errorCode = "too_many_turns", .content = "The model took too many turns", .turnsUsed = maxTurns };
    }

    // Writes the session manifest before the first request and rewrites it with the outcome afterwards; a throw
    // leaves the manifest marked incomplete, which the harness refuses.
    PromptResult Prompt(std::string_view prompt, unsigned maxTurns)
    {
        m_recorder.BeginSession(maxTurns, prompt);

        PromptResult result{};
        try
        {
            result = RunPrompt(prompt, maxTurns);
        }
        catch (std::exception const& error)
        {
            m_recorder.EndSession(json{
                { "ok"       , false          },
                { "errorCode", "exception"    },
                { "content"  , error.what()   },
                { "turnsUsed", 0u             },
            });
            throw;
        }

        m_recorder.EndSession(static_cast<json>(result));
        return result;
    }


    Endpoint                         m_endpointDescriptor;
    ToolsRuntimeContext              m_toolContext;
    std::string                      m_modelName;
    std::vector<ToolDefinition>      m_tools;
    unsigned                         m_maxTokens = 0;

    std::vector<ConversationMessage> m_conversationHistory;
    LogFileState                     m_log;
    // Writes <id>_session.json beside the per-turn log files; best effort, never throws.
    SessionRecorder                  m_recorder;
};

Session::PromptResult::operator nlohmann::json() const
{
    return {
        { "ok"       , ok        },
        { "errorCode", errorCode },
        { "content"  , content   },
        { "turnsUsed", turnsUsed }
    };
}

Session::Session(Config config) : m_pimpl{ std::make_unique<Pimpl>(std::move(config)) } {}

Session::~Session() = default;

Session::Session(Session&&) = default;

Session& Session::operator=(Session&&) = default;

Session::PromptResult Session::Prompt(std::string_view prompt, unsigned maxTurns)
{
    return m_pimpl->Prompt(prompt, maxTurns);
}