
#include "NetworkUtilities.h"

#include "FileUtilities.h"
#include "StringUtilities.h"

#include <httplib.h>

#include <mutex>
#include <print>
#include <stdexcept>
#include <utility>

Endpoint ParseEndpoint(std::string_view const endpoint)
{
    Endpoint result{};

    std::string const scheme = "http://";
    if (endpoint.rfind(scheme, 0) != 0)
    {
        throw std::runtime_error("Only http:// endpoints are supported");
    }

    std::string_view const target = endpoint.substr(scheme.size());
    std::size_t const slash_pos = target.find('/');
    std::string_view authority;
    if (slash_pos != std::string::npos)
    {
        authority = target.substr(0, slash_pos);
        result.path = target.substr(slash_pos);
    }
    else
    {
        authority = target;
        result.path = "/";
    }

    std::size_t const colon_pos = authority.find(':');
    if (colon_pos != std::string::npos)
    {
        result.host = authority.substr(0, colon_pos);
        result.port = static_cast<std::uint32_t>(std::stoul(std::string{authority.substr(colon_pos + 1)}));
    }
    else
    {
        result.host = authority;
        result.port = 80;
    }

    return result;
}

std::string HttpPost(Endpoint const& endpoint, std::string_view const payloadType, std::string_view const payload, std::function<void(std::string_view)> const& sseCallback)
{
    httplib::Client cli(endpoint.host, static_cast<int>(endpoint.port));

    std::string buffer;
    std::string sseChunk;
    auto res = cli.Post(endpoint.path, httplib::Headers{}, std::string{ payload }, std::string{ payloadType },
        [&](char const *data, size_t len)
        {
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
        }
    );

    if (res)
    {
        return res->body;
    }
    else
    {
        return "HTTP error";
    }
}
