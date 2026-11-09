#pragma once

#include "JsonUtilities.h"
#include "ToolUtilities.h"

#include <string>
#include <vector>

// Why a response stopped. `notFinished` is the state of a streaming response before the service reports a
// real reason, and of any response that never reported one.
enum class ResponseFinishReason
{
    notFinished   ,
    stop          ,
    length        ,
    tool_calls    ,
    content_filter,
};

std::string to_string(ResponseFinishReason reason);

// One complete model response, normalised so that callers never see the endpoint flavour.
struct ModelResponse
{
    ResponseFinishReason  reason = ResponseFinishReason::notFinished;
    std::string           reasoning;
    std::string           content;
    std::vector<ToolCall> toolCalls;
};

// One streaming increment of a response; `reason` stays `notFinished` until the service reports it.
struct ModelResponseDelta
{
    ResponseFinishReason       reason = ResponseFinishReason::notFinished;
    std::string                reasoning;
    std::string                content;
    std::vector<ToolCallDelta> toolCalls;
};

// Parses one server-sent event of a streaming OpenAI-compatible response. Throws std::runtime_error on a
// malformed event, and debug-logs a legitimate event that carries only token usage.
ModelResponseDelta ExtractModelContentDelta(json const& event);

// Parses a complete response body. `ollamaStyle` selects the `/api/chat` layout instead of the
// OpenAI-compatible `choices` layout. Throws std::runtime_error on a malformed response.
ModelResponse ExtractModelContent(json const& response, bool ollamaStyle);
