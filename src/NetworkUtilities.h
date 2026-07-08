#pragma once

#if _WIN32
typedef unsigned __int64 SOCKET;
#define INVALID_SOCKET  (SOCKET)(~0)
#else
typedef int SOCKET;
#define INVALID_SOCKET  -1
#endif

#include <string>
#include <string_view>
#include <utility>

struct Endpoint
{
    std::string host;
    std::string port;
    std::string path;
};

Endpoint ParseEndpoint(std::string_view endpoint);

class Socket
{
public:
    Socket() = default;
    ~Socket();

    static Socket Connect(Endpoint const& endpoint);
    void Close() noexcept;

    Socket(Socket const&) = delete;
    Socket& operator=(Socket const&) = delete;

    Socket(Socket&& other) noexcept : m_sock(std::exchange(other.m_sock, INVALID_SOCKET)) {}
    Socket& operator=(Socket&& other) noexcept;

    void Send(std::string_view data, std::string_view description = {});
    std::string Receive(std::string_view description = {});

#if _WIN32
    explicit operator bool() { return m_sock != INVALID_SOCKET; }
#else
    explicit operator bool() { return m_sock >= 0; }
#endif

private:
    SOCKET m_sock = INVALID_SOCKET;
};
