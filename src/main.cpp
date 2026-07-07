
#include "FileUtilities.h"
#include "StringUtilities.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct ToolCall
{
    std::string name;
    std::string path;
    std::string content;
};

ToolCall ParseToolCall(std::string_view const text)
{
    ToolCall tool;
    std::regex json_regex(
        R"(\{\s*\"tool\"\s*:\s*\"([^\"]+)\"\s*,\s*\"path\"\s*:\s*\"([^\"]*)\"(?:,\s*\"content\"\s*:\s*\"([^\"]*)\")?\s*\})");
    std::cmatch match;
    if (std::regex_search(text.begin(), text.end(), match, json_regex))
    {
        tool.name = match[1].str();
        tool.path = UnescapeJsonString(match[2].str());
        if (match[3].matched)
        {
            tool.content = UnescapeJsonString(match[3].str());
        }
        return tool;
    }

    std::regex simple_regex(R"(^\s*TOOL\s+([A-Za-z_]+)\s+(.+)$)");
    if (std::regex_search(text.begin(), text.end(), match, simple_regex))
    {
        tool.name = match[1].str();
        std::string const remainder = match[2].str();
        std::size_t const split = remainder.find(' ');
        if (split != std::string::npos)
        {
            tool.path = remainder.substr(0, split);
            tool.content = remainder.substr(split + 1);
        }
        else
        {
            tool.path = remainder;
        }
        return tool;
    }
    return tool;
}

std::string ExecuteTool(ToolCall const& tool)
{
    try
    {
        if (tool.name == "read_file")
        {
            return ReadTextFile(tool.path);
        }
        if (tool.name == "write_file")
        {
            WriteTextFile(tool.path, tool.content);
            return "[write_file] ok";
        }
    }
    catch(std::exception const e)
    {
        std::cerr << '[' << tool.name << "] " << e.what() << '\n';
        return e.what();
    }
    return "[tool] unknown tool";
}

std::string BuildPayload(std::string_view const system_prompt, std::string_view const user_prompt)
{
    std::filesystem::path const payload_path = GetExecutableDirectory() / "data" / "PayloadTemplate.json";
    std::string result = ReadTextFile(payload_path);

    ReplaceIn(result, "@@system_prompt@@", EscapeJsonString(system_prompt));
    ReplaceIn(result, "@@user_prompt@@"  , EscapeJsonString(user_prompt)  );

    return result;
}

std::string ExtractModelContent(std::string_view const response_body)
{
    std::regex content_regex(R"REGEX("content"\s*:\s*"((?:[^"\\]|\\.)*)")REGEX");
    std::cmatch match;
    if (std::regex_search(response_body.begin(), response_body.end(), match, content_regex))
    {
        return UnescapeJsonString(match[1].str());
    }

    std::regex text_regex(R"REGEX("text"\s*:\s*"((?:[^"\\]|\\.)*)")REGEX");
    if (std::regex_search(response_body.begin(), response_body.end(), match, text_regex))
    {
        return UnescapeJsonString(match[1].str());
    }

    return { response_body.begin(), response_body.end() };
}

struct Endpoint
{
    std::string host;
    std::string port;
    std::string path;
};

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

int ConnectSocket(Endpoint const& endpoint)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* info = nullptr;
    int const gai = getaddrinfo(endpoint.host.c_str(), endpoint.port.c_str(), &hints, &info);
    if (gai != 0)
    {
        throw std::runtime_error(std::string("getaddrinfo failed: ") + gai_strerror(gai));
    }

    int sock = -1;
    for (addrinfo* current = info; current != nullptr; current = current->ai_next)
    {
        printf("Trying to connect to %s:%s\n", endpoint.host.c_str(), endpoint.port.c_str());
        sock = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (sock < 0)
        {
            continue;
        }
        if (connect(sock, current->ai_addr, current->ai_addrlen) == 0)
        {
            break;
        }
        printf("Failed to connect to %s:%s\n", endpoint.host.c_str(), endpoint.port.c_str());
        close(sock);
        sock = -1;
    }
    freeaddrinfo(info);

    if (sock < 0)
    {
        throw std::runtime_error("unable to connect to the llama.cpp server");
    }

    return sock;
}

std::string HttpPostJson(Endpoint const& endpoint, std::string const& payload)
{
    auto sock = ConnectSocket(endpoint);

    std::string request = ReadTextFile(GetExecutableDirectory() / "data" / "HttpPostJsonTemplate.txt");
    ReplaceNewlinesIn(request, "\r\n"); // HTTP 1.1 requires CRLF line endings.
    ReplaceIn(request, "@@endpointPath@@", endpoint.path);
    ReplaceIn(request, "@@endpointHost@@", endpoint.host);
    ReplaceIn(request, "@@endpointPort@@", endpoint.port);
    ReplaceIn(request, "@@payloadSize@@", std::to_string(payload.size()));
    ReplaceIn(request, "@@payload@@", payload);

    //printf("Sending HTTP request:\n%s\n", request.c_str());

    if (send(sock, request.c_str(), request.size(), 0) < 0)
    {
        close(sock);
        throw std::runtime_error("failed to send HTTP request");
    }

    std::string response;
    char buffer[4096];
    for (;;)
    {
        ssize_t const received = recv(sock, buffer, sizeof(buffer), 0);
        if (received <= 0)
        {
            break;
        }
        response.append(buffer, static_cast<std::size_t>(received));
    }
    close(sock);

    //printf("Response:\n%s\n", response.c_str());

    std::size_t const header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos)
    {
        throw std::runtime_error("malformed HTTP response");
    }

    std::string const body = response.substr(header_end + 4);
    std::string const status_line = response.substr(0, response.find('\r'));
    if (status_line.find("200") == std::string::npos)
    {
        throw std::runtime_error("server returned: " + status_line);
    }
    return ExtractModelContent(body);
}

} // namespace

int main(int argc, char** argv)
{
    std::string endpoint = "http://127.0.0.1:8080/v1/chat/completions";
    std::string prompt;
    int max_turns = 3;

    for (int i = 1; i < argc; ++i)
    {
        std::string const arg = argv[i];
        if (arg == "--endpoint" && i + 1 < argc)
        {
            endpoint = argv[++i];
        }
        else if (arg == "--max-turns" && i + 1 < argc)
        {
            max_turns = std::stoi(argv[++i]);
        }
        else if (prompt.empty())
        {
            prompt = arg;
        }
        else
        {
            prompt += " ";
            prompt += arg;
        }
    }

    if (prompt.empty())
    {
        prompt = "Read the README.md file and summarise it in one paragraph.";
    }

    Endpoint const endpointDescriptor = ParseEndpoint(endpoint);

    std::string const system_prompt = ReadTextFile(GetExecutableDirectory() / "data" / "SystemPrompt.txt");

    std::string conversation_prompt = prompt;
    for (int turn = 0; turn < max_turns; ++turn)
    {
        try
        {
            std::string const payload = BuildPayload(system_prompt, conversation_prompt);
            //printf("Sending request to %s with payload:\n%s\n", endpoint.c_str(), payload.c_str());
            std::string const assistant_reply = HttpPostJson(endpointDescriptor, payload);
            std::cout << "assistant> " << assistant_reply << std::endl;

            ToolCall tool = ParseToolCall(assistant_reply);
            if (tool.name.empty())
            {
                break;
            }

            std::string const tool_result = ExecuteTool(tool);
            std::cout << "tool> " << tool.name << " -> " << EscapeJsonString(tool_result.substr(0, 20)) << (tool_result.size() > 20 ? "..." : "") << std::endl;
            conversation_prompt =
                "Tool call: " + tool.name + " path=" + tool.path + "\nResult: " + tool_result + "\nContinue the task.";
        }
        catch (std::exception const& error)
        {
            std::cerr << "agent error: " << error.what() << std::endl;
            return 1;
        }
    }

    return 0;
}
