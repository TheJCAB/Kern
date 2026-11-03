#pragma once

#include "Endpoint.h"

#include <functional>
#include <string>
#include <string_view>

struct HttpResult
{
    int         status = 0;  // 0 if no response was received
    std::string error;       // transport-level failure (connection, TLS, timeout...); empty if a response arrived
    std::string body;        // the complete raw response body, with the API key redacted
    int         retryAfterSeconds = 0; // from the Retry-After header, if the server sent one in seconds

    bool IsSuccess() const { return error.empty() && status >= 200 && status < 300; }
};

// Throws only if the request is refused before being sent; otherwise failures are reported in the result.
HttpResult HttpPost(Endpoint const& endpoint, std::string_view payloadType, std::string_view payload, std::function<void(std::string_view)> const&);
