#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

struct Endpoint
{
    std::string   host;
    std::uint32_t port;
    std::string   path;
};

Endpoint ParseEndpoint(std::string_view endpoint);

std::string HttpPost(Endpoint const& endpoint, std::string_view payloadType, std::string_view payload, std::function<void(std::string_view)> const&);
