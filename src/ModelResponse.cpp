#include "ModelResponse.h"
#include "Logging.h"

#include <format>
#include <print>
#include <stdexcept>
#include <string>

std::string to_string(ResponseFinishReason const reason)
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

ModelResponse ExtractModelContent(json const& response, bool ollamaStyle)
{
    ModelResponse result;

    if (!response.is_object()) throw std::runtime_error("malformed response: not a JSON object");

    if (ollamaStyle)
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
