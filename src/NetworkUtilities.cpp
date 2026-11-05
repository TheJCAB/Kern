
#include "NetworkUtilities.h"

#include "FileUtilities.h"
#include "Logging.h"
#include "StringUtilities.h"

#include <httplib.h>

#include <chrono>
#include <mutex>
#include <print>
#include <stdexcept>
#include <utility>

HttpResult HttpPost(Endpoint const& endpoint, std::string_view const payloadType, std::string_view const payload, std::function<void(std::string_view)> const& sseCallback)
{
    // Defense in depth: callers must have vetted the endpoint, but never send a key in cleartext to a remote host regardless.
    if (!endpoint.apiKey.empty() && !endpoint.secure && !IsLoopbackHost(endpoint.host))
    {
        throw std::runtime_error("Refusing to send an API key over an unencrypted connection");
    }

    std::string const hostForUrl = endpoint.host.find(':') != std::string::npos ? "[" + endpoint.host + "]" : endpoint.host;
    httplib::Client cli(std::string(endpoint.secure ? "https://" : "http://") + hostForUrl + ":" + std::to_string(endpoint.port));
    cli.set_decompress(false); // We ask for identity encoding; some servers echo "Content-Encoding: identity", which httplib would reject.
    cli.set_error_logger([](httplib::Error const error, httplib::Request const*)
    {
        Log(LogLevel::debug, "httplib error: {}", httplib::to_string(error));
    });

    httplib::Headers headers;
    if (!endpoint.apiKey.empty())
    {
        headers.emplace("Authorization", "Bearer " + endpoint.apiKey);
    }

    std::string rawBody;
    std::string buffer;
    std::string sseChunk;
    std::size_t eventCount = 0;

    Log(LogLevel::debug, "POST {}://{}:{}{} ({} bytes, API key: {})",
        endpoint.secure ? "https" : "http", endpoint.host, endpoint.port, endpoint.path, payload.size(), endpoint.apiKey.empty() ? "none" : "sent as bearer token");
    auto const start = std::chrono::steady_clock::now();

    httplib::Request request;
    request.method = "POST";
    request.path = endpoint.path;
    request.headers = headers;
    request.headers.emplace("Content-Type", std::string{ payloadType });
    // SSE gains nothing from compression, and streaming decompression is a needless source of failures.
    request.headers.emplace("Accept-Encoding", "identity");
    request.body = std::string{ payload };

    int responseStatus = 0;
    int retryAfterSeconds = 0;
    // Only response headers are logged, never request headers (which carry the API key).
    request.response_handler = [&](httplib::Response const& response)
    {
        responseStatus = response.status;
        if (auto const retryAfter = response.get_header_value("Retry-After"); !retryAfter.empty() && retryAfter.size() <= 4 && retryAfter.find_first_not_of("0123456789") == std::string::npos)
        {
            retryAfterSeconds = std::stoi(retryAfter);
        }
        auto const encoding = response.get_header_value("Content-Encoding");
        if (!encoding.empty() && encoding != "identity")
        {
            Log(LogLevel::error, "the server compressed the response ({}) despite being asked not to; Kern can't decode it", encoding);
            return false;
        }
        Log(LogLevel::debug, "response headers: HTTP {}, Content-Type: '{}', Content-Encoding: '{}', Transfer-Encoding: '{}', Content-Length: '{}'",
            response.status,
            response.get_header_value("Content-Type"),
            response.get_header_value("Content-Encoding"),
            response.get_header_value("Transfer-Encoding"),
            response.get_header_value("Content-Length"));
        return true;
    };

    request.content_receiver =
        [&](char const *data, size_t len, size_t /*offset*/, size_t /*total*/)
        {
            rawBody.append(data, len);
            buffer.append(data, len);

            size_t pos = 0;

            for (;;)
            {
                // Find next newline
                size_t newline = buffer.find('\n', pos);
                if (newline == std::string::npos)
                {
                    break; // incomplete line, wait for more chunks
                }

                auto line = std::string_view{ buffer.data() + pos, newline - pos };
                pos = newline + 1;

                // Trim CR
                if (!line.empty() && line.back() == '\r')
                {
                    line = line.substr(0, line.size() - 1);
                }

                // Empty line = event boundary
                if (line.empty())
                {
                    auto const donePos = sseChunk.find("[DONE]");
                    if (donePos != sseChunk.npos && donePos <= 1)
                    {
                        sseChunk = {};
                    }
                    else if (!sseChunk.empty())
                    {
                        ++eventCount;
                        Log(LogLevel::trace, "SSE event {}: {}", eventCount, sseChunk);
                        sseCallback(std::exchange(sseChunk, {}));
                    }
                    continue;
                }

                // Parse SSE fields
                auto starts_with = [](std::string_view s, std::string_view prefix) { return s.compare(0, prefix.size(), prefix) == 0; };
                if (starts_with(line, "data:"))
                {
                    sseChunk += line.substr(5);
                    sseChunk += "\n";
                }
                // We ignore these:
                //else if (starts_with(line, "event:"))
                //else if (starts_with(line, "id:"))
            }

            // Remove processed part
            buffer.erase(0, pos);
            return true; // continue streaming
        };

    auto res = cli.send(request);

    // Unnecessary, but... Runware keeps insisting that all our requests are left pending, somehow.
    // It's unsure whether this would help with this problem, but it's worth a shot.
    cli.stop();

    HttpResult result;
    result.status = responseStatus;
    result.retryAfterSeconds = retryAfterSeconds;
    if (res)
    {
        result.status = res->status;
        if (rawBody.empty())
        {
            rawBody = res->body; // not streamed (e.g. some error responses)
        }
    }
    else
    {
        result.error = httplib::to_string(res.error());
    }
    // A misbehaving server could echo the key back; make sure it can never reach logs or error messages.
    if (!endpoint.apiKey.empty())
    {
        ReplaceIn(rawBody, endpoint.apiKey, "[redacted]");
    }
    result.body = std::move(rawBody);

    auto const elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    if (result.error.empty())
    {
        Log(LogLevel::debug, "HTTP {} after {} ms: {} bytes, {} SSE events", result.status, elapsedMs, result.body.size(), eventCount);
    }
    else
    {
        Log(LogLevel::error, "HTTP request failed after {} ms: {} (HTTP status seen: {}, {} bytes received, {} SSE events)",
            elapsedMs, result.error, result.status, result.body.size(), eventCount);
        if (!result.body.empty())
        {
            Log(LogLevel::error, "partial response body: {}", result.body.substr(0, 1000));
        }
    }
    return result;
}
