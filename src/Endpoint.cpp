#include "Endpoint.h"

#include <stdexcept>
#include <string>

Endpoint ParseEndpoint(std::string_view const endpoint)
{
    Endpoint result{};

    std::string_view target;
    if (endpoint.starts_with("http://"))
    {
        target = endpoint.substr(7);
        result.port = 80;
    }
    else if (endpoint.starts_with("https://"))
    {
        target = endpoint.substr(8);
        result.secure = true;
        result.port = 443;
    }
    else
    {
        throw std::runtime_error("Only http:// and https:// endpoints are supported");
    }

    std::size_t const slashPos = target.find('/');
    std::string_view authority = target.substr(0, slashPos);
    result.path = slashPos == std::string_view::npos ? "/" : std::string{ target.substr(slashPos) };

    if (authority.find('@') != std::string_view::npos)
    {
        throw std::runtime_error("Credentials in the URL are not supported");
    }

    std::string_view portText;
    if (authority.starts_with('['))
    {
        std::size_t const close = authority.find(']');
        if (close == std::string_view::npos)
        {
            throw std::runtime_error("Unterminated IPv6 address");
        }
        result.host = authority.substr(1, close - 1);
        std::string_view const rest = authority.substr(close + 1);
        if (!rest.empty())
        {
            if (!rest.starts_with(':'))
            {
                throw std::runtime_error("Invalid authority");
            }
            portText = rest.substr(1);
        }
    }
    else
    {
        std::size_t const colonPos = authority.find(':');
        result.host = authority.substr(0, colonPos);
        if (colonPos != std::string_view::npos)
        {
            portText = authority.substr(colonPos + 1);
        }
    }

    if (result.host.empty())
    {
        throw std::runtime_error("Missing host");
    }

    if (!portText.empty())
    {
        unsigned long value = 0;
        for (char const c : portText)
        {
            if (c < '0' || c > '9' || (value = value * 10 + static_cast<unsigned long>(c - '0')) > 65535ul)
            {
                throw std::runtime_error("Invalid port");
            }
        }
        if (value == 0)
        {
            throw std::runtime_error("Invalid port");
        }
        result.port = static_cast<std::uint32_t>(value);
    }

    return result;
}

bool IsLoopbackHost(std::string_view const host)
{
    if (host == "localhost" || host == "::1")
    {
        return true;
    }
    if (!host.starts_with("127."))
    {
        return false;
    }
    for (char const c : host)
    {
        if ((c < '0' || c > '9') && c != '.')
        {
            return false;
        }
    }
    return true;
}
