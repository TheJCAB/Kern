#pragma once

#include "Endpoint.h"
#include "JsonUtilities.h"
#include "LogFile.h"
#include "ModelResponse.h"

#include <string>

// One request/response exchange with the service: sends a request payload, sleeps and retries the transient HTTP
// failures, saves the request and response artifacts, and normalises the answer into a ModelResponse. The endpoint
// flavour decides whether the answer arrives as a server-sent event stream or as one complete body, so callers
// never see the difference.
class ChatTransport
{
public:
    struct Exchange
    {
        ModelResponse response;
        json          usage        = json::object(); // as the service reported it; empty when it reported none
        double        milliseconds = 0.0;            // round-trip time of the request that produced the response
    };

    ChatTransport(Endpoint endpoint, LogFile& log);

    // True for the Ollama /api/chat and /api/generate layouts, which do not use the OpenAI-compatible fields.
    bool IsOllama() const;

    // Sends `payload` and returns the normalised answer, saving the request artifact and, at debug level, the raw
    // response body. Throws std::runtime_error when the transport fails or the answer is malformed.
    Exchange Send(json const& payload);

private:
    Endpoint m_endpoint;
    LogFile& m_log;
};
