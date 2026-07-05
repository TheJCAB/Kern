#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

struct ToolCall
{
    std::string name;
    std::string path;
    std::string content;
};

std::string escape_json_string(std::string const& input)
{
    std::string out;
    for (char const c : input)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

std::string unescape_json_string(std::string const& input)
{
    std::string out;
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] == '\\' && i + 1 < input.size())
        {
            ++i;
            switch (input[i])
            {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            default:
                out.push_back(input[i]);
                break;
            }
        }
        else
        {
            out.push_back(input[i]);
        }
    }
    return out;
}

std::string read_text_file(std::filesystem::path const& path)
{
    std::ifstream input(path);
    if (!input)
    {
        return "[read_file] error: could not open file";
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string write_text_file(std::filesystem::path const& path, std::string const& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::trunc);
    if (!output)
    {
        return "[write_file] error: could not write file";
    }
    output << content;
    return "[write_file] ok";
}

ToolCall parse_tool_call(std::string const& text)
{
    ToolCall tool;
    std::regex json_regex(
        R"(\{\s*\"tool\"\s*:\s*\"([^\"]+)\"\s*,\s*\"path\"\s*:\s*\"([^\"]*)\"(?:,\s*\"content\"\s*:\s*\"([^\"]*)\")?\s*\})");
    std::smatch match;
    if (std::regex_search(text, match, json_regex))
    {
        tool.name = match[1].str();
        tool.path = unescape_json_string(match[2].str());
        if (match[3].matched)
        {
            tool.content = unescape_json_string(match[3].str());
        }
        return tool;
    }

    std::regex simple_regex(R"(^\s*TOOL\s+([A-Za-z_]+)\s+(.+)$)");
    if (std::regex_search(text, match, simple_regex))
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

std::string execute_tool(ToolCall const& tool)
{
    if (tool.name == "read_file")
    {
        return read_text_file(tool.path);
    }
    if (tool.name == "write_file")
    {
        return write_text_file(tool.path, tool.content);
    }
    return "[tool] unknown tool";
}

std::string build_payload(std::string const& system_prompt, std::string const& user_prompt)
{
    std::ostringstream payload;
    payload << "{\"model\":\"gemma-4\",\"messages\":["
            << "{\"role\":\"system\",\"content\":\"" << escape_json_string(system_prompt) << "\"},"
            << "{\"role\":\"user\",\"content\":\"" << escape_json_string(user_prompt) << "\"}]}";
    return payload.str();
}

std::string extract_model_content(std::string const& response_body)
{
    std::regex content_regex(R"REGEX("content"\s*:\s*"((?:[^"\\]|\\.)*)")REGEX");
    std::smatch match;
    if (std::regex_search(response_body, match, content_regex))
    {
        return unescape_json_string(match[1].str());
    }

    std::regex text_regex(R"REGEX("text"\s*:\s*"((?:[^"\\]|\\.)*)")REGEX");
    if (std::regex_search(response_body, match, text_regex))
    {
        return unescape_json_string(match[1].str());
    }

    return response_body;
}

std::string http_post_json(std::string const& endpoint, std::string const& payload)
{
    std::string const scheme = "http://";
    if (endpoint.rfind(scheme, 0) != 0)
    {
        throw std::runtime_error("Only http:// endpoints are supported");
    }

    std::string const target = endpoint.substr(scheme.size());
    std::size_t const slash_pos = target.find('/');
    std::string authority = target;
    std::string path = "/";
    if (slash_pos != std::string::npos)
    {
        authority = target.substr(0, slash_pos);
        path = target.substr(slash_pos);
    }

    std::size_t const colon_pos = authority.find(':');
    std::string host = authority;
    std::string port = "80";
    if (colon_pos != std::string::npos)
    {
        host = authority.substr(0, colon_pos);
        port = authority.substr(colon_pos + 1);
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* info = nullptr;
    int const gai = getaddrinfo(host.c_str(), port.c_str(), &hints, &info);
    if (gai != 0)
    {
        throw std::runtime_error(std::string("getaddrinfo failed: ") + gai_strerror(gai));
    }

    int sock = -1;
    for (addrinfo* current = info; current != nullptr; current = current->ai_next)
    {
        printf("Trying to connect to %s:%s\n", host.c_str(), port.c_str());
        sock = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (sock < 0)
        {
            continue;
        }
        if (connect(sock, current->ai_addr, current->ai_addrlen) == 0)
        {
            break;
        }
        printf("Failed to connect to %s:%s\n", host.c_str(), port.c_str());
        close(sock);
        sock = -1;
    }
    freeaddrinfo(info);

    if (sock < 0)
    {
        throw std::runtime_error("unable to connect to the llama.cpp server");
    }

    std::ostringstream request;
    request << "POST " << path << " HTTP/1.1\r\n"
            << "Host: " << host << ":" << port << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << payload.size() << "\r\n"
            << "Connection: close\r\n"
            << "\r\n"
            << payload;

    std::string const request_text = request.str();
    if (send(sock, request_text.c_str(), request_text.size(), 0) < 0)
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
    return extract_model_content(body);
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

    std::string const system_prompt =
        "(You are a tiny agent harness. When you need a file, emit one plain-line tool call as JSON like "
        "{\"tool\":\"read_file\",\"path\":\"/tmp/file\"} or "
        "{\"tool\":\"write_file\",\"path\":\"/tmp/file\",\"content\":\"...\"}."
        "If no tool is needed, answer directly.)SYS";

    std::string conversation_prompt = prompt;
    for (int turn = 0; turn < max_turns; ++turn)
    {
        try
        {
            std::string const payload = build_payload(system_prompt, conversation_prompt);
            printf("Sending request to %s with payload:\n%s\n", endpoint.c_str(), payload.c_str());
            std::string const assistant_reply = http_post_json(endpoint, payload);
            std::cout << "assistant> " << assistant_reply << std::endl;

            ToolCall tool = parse_tool_call(assistant_reply);
            if (tool.name.empty())
            {
                break;
            }

            std::string const tool_result = execute_tool(tool);
            std::cout << "tool> " << tool.name << " -> " << tool_result << std::endl;
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
