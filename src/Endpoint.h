#pragma once

#include <cstdint>
#include <string>
#include <string_view>

struct Endpoint
{
    std::string   host;
    std::uint32_t port   = 0;
    std::string   path;
    bool          secure = false; // true for https

    // Secret. Sent as a Bearer token if not empty. Never print or log this.
    std::string   apiKey;
};

// Accepts http:// and https:// URLs. Throws std::runtime_error if invalid.
// The scheme's default port is used if none is specified.
Endpoint ParseEndpoint(std::string_view endpoint);

// True for localhost, 127.x.x.x and ::1.
bool IsLoopbackHost(std::string_view host);
