
#include "NetworkUtilities.h"

#if _WIN32
#include <Ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <mutex>
#include <stdexcept>

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
        result.port = authority.substr(colon_pos + 1);
    }
    else
    {
        result.host = authority;
        result.port = "80";
    }

    return result;
}

Socket Socket::Connect(Endpoint const& endpoint)
{
    Socket result;

#if _WIN32
    using addrinfo = ADDRINFOA;
    #define gai_strerror gai_strerrorA

    static WSADATA wsaData =
        []()
        {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            {
                throw std::runtime_error("WSAStartup failed.");
            }
            return wsaData;
        }
        ();
#endif

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* info = nullptr;
    int const gai = getaddrinfo(endpoint.host.c_str(), endpoint.port.c_str(), &hints, &info);
    if (gai != 0)
    {
        throw std::runtime_error(std::string("getaddrinfo failed: ") + gai_strerror(gai));
    }

    for (auto* current = info; current != nullptr; current = current->ai_next)
    {
        printf("Trying to connect to %s:%s\n", endpoint.host.c_str(), endpoint.port.c_str());
        result.m_sock = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (!result)
        {
            continue;
        }
        if (connect(result.m_sock, current->ai_addr, current->ai_addrlen) == 0)
        {
            break;
        }
        printf("Failed to connect to %s:%s\n", endpoint.host.c_str(), endpoint.port.c_str());
        result = {};
    }
    freeaddrinfo(info);

    if (!result)
    {
        throw std::runtime_error("unable to connect to the llama.cpp server");
    }

    return result;
}

Socket::~Socket()
{
    Close();
}

void Socket::Close() noexcept
{
    if (*this)
    {
#if _WIN32
        closesocket(std::exchange(m_sock, INVALID_SOCKET));
#else
        close(std::exchange(m_sock, INVALID_SOCKET));
#endif
    }
}

Socket& Socket::operator=(Socket&& other) noexcept
{
    auto const sock = std::exchange(other.m_sock, INVALID_SOCKET);
    Close();
    m_sock = sock;
    return *this;
}

void Socket::Send(std::string_view const data, std::string_view const description)
{
    if (!*this) throw std::runtime_error("socket is not connected");

    if (send(m_sock, data.data(), data.size(), 0) < 0)
    {
        Close();
        if (description.empty())
        {
            throw std::runtime_error("failed to send data over socket");
        }
        else
        {
            throw std::runtime_error("failed to send data over socket: " + std::string(description));
        }
    }
}

std::string Socket::Receive(std::string_view const /*description*/)
{
    if (!*this) throw std::runtime_error("socket is not connected");

    std::string response;
    char buffer[4096];
    for (;;)
    {
        auto const received = recv(m_sock, buffer, sizeof(buffer), 0);
        if (received <= 0)
        {
            break;
        }
        response.append(buffer, static_cast<std::size_t>(received));
    }
    return response;
}

