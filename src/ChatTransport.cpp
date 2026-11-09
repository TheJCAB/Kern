#include "ChatTransport.h"

#include "Logging.h"
#include "NetworkUtilities.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    HttpResult PostWithRetry(Endpoint const& endpoint, json const& payload, std::function<void(std::string_view)> const& sseCallback)
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

    void CheckHttpResult(HttpResult const& result)
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
}

ChatTransport::ChatTransport(Endpoint endpoint, LogFile& log)
    : m_endpoint{ std::move(endpoint) }
    , m_log     { log              }
{
}

bool ChatTransport::IsOllama() const
{
    return m_endpoint.path.find("/api/chat") != std::string::npos || m_endpoint.path.find("/api/generate") != std::string::npos;
}

ChatTransport::Exchange ChatTransport::Send(json const& payload)
{
    Exchange exchange;
    ModelResponse modelResponse;

    m_log.Write("request", payload.dump(2));

    if (IsOllama())
    {
        auto const requestStart = std::chrono::steady_clock::now();
        HttpResult const httpResult = PostWithRetry(m_endpoint, payload, [](auto){});
        exchange.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - requestStart).count();
        if (IsLogEnabled(LogLevel::debug))
        {
            m_log.Write("response_raw", httpResult.body, "txt");
        }
        CheckHttpResult(httpResult);
        std::string const& responseBody = httpResult.body;
        //std::cout << "Response: " << responseBody << std::endl;

        json const response = json::parse(responseBody.begin(), responseBody.end(), nullptr, false);
        if (response.contains("usage"))
        {
            exchange.usage = response.at("usage");
        }
        m_log.Write("response", response.dump(2));

        exchange.response = ExtractModelContent(response, IsOllama());
        Log(LogLevel::debug, "finish_reason: {}, content: {} chars, tool calls: {}",
            to_string(exchange.response.reason), exchange.response.content.size(), exchange.response.toolCalls.size());
        return exchange;
    }

    json response;

    std::map<std::size_t, ToolCallDelta> toolCalls;

    json accumulatedUsage = json::object();

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
    HttpResult const httpResult = PostWithRetry(m_endpoint, payload, sseCallback);
    exchange.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - requestStart).count();
    if (IsLogEnabled(LogLevel::debug))
    {
        m_log.Write("response_raw", httpResult.body, "txt");
    }
    CheckHttpResult(httpResult);

    std::println();

    exchange.usage = std::move(accumulatedUsage);

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

    m_log.Write("response", assembledResponse.dump(2));

    exchange.response = std::move(modelResponse);
    return exchange;
}
