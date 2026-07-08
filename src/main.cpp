
#include "FileUtilities.h"
#include "NetworkUtilities.h"
#include "StringUtilities.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <ranges>
#include <regex>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using json = nlohmann::json;

namespace
{

struct ToolParameter
{
    std::string_view name;
    std::string_view description;
};

using ToolCall = std::string(json const& toolJson);

struct ToolDefinition
{
    std::string_view               name;
    std::string_view               description;
    std::span<ToolParameter const> parameters;
    ToolCall&                      callTool;
};    

constexpr ToolParameter ReadFileToolParameters[] =
{
    { "path"   , "The path to the file to read." },
};    

constexpr ToolParameter WriteFileToolParameters[] =
{
    { "path"   , "The path to the file to write."    },
    { "content", "The content to write to the file." },
};    

std::string ReadFileTool(json const& toolJson)
{
    auto& path = toolJson["path"].get_ref<std::string const&>();
    return ReadTextFile(path);
}

std::string WriteFileTool(json const& toolJson)
{
    auto& path    = toolJson["path"   ].get_ref<std::string const&>();
    auto& content = toolJson["content"].get_ref<std::string const&>();

    WriteTextFile(path, content);
    return "[write_file] ok";
}

constexpr ToolDefinition const Tools[] =
{
    { "read_file" , "Read the contents of a file.", ReadFileToolParameters , ReadFileTool  },
    { "write_file", "Write content to a file."    , WriteFileToolParameters, WriteFileTool },
};

std::optional<std::string> ParseToolCall(std::string_view const text)
{
    json const toolCall = json::parse(text.begin(), text.end(), nullptr, false);
    if (!toolCall.is_object())
    {
        return {};
    }

    auto& tool = toolCall["tool"].get_ref<std::string const&>();

    for (auto&& toolDef : Tools)
    {
        if (tool == toolDef.name)
        {
            try
            {
                return toolDef.callTool(toolCall);
            }
            catch (std::exception const e)
            {
                std::cerr << '[' << toolDef.name << "] " << e.what() << '\n';
                throw std::runtime_error{ e.what() };
            }
        }
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
    std::match_results<std::string_view::const_iterator> match;
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

std::string HttpPostJson(Endpoint const& endpoint, std::string const& payload)
{
    auto sock = Socket::Connect(endpoint);

    std::string request = ReadTextFile(GetExecutableDirectory() / "data" / "HttpPostJsonTemplate.txt");
    ReplaceNewlinesIn(request, "\r\n"); // HTTP 1.1 requires CRLF line endings.
    ReplaceIn(request, "@@endpointPath@@", endpoint.path);
    ReplaceIn(request, "@@endpointHost@@", endpoint.host);
    ReplaceIn(request, "@@endpointPort@@", endpoint.port);
    ReplaceIn(request, "@@payloadSize@@", std::to_string(payload.size()));
    ReplaceIn(request, "@@payload@@", payload);

    //printf("Sending HTTP request:\n%s\n", request.c_str());

    sock.Send(request, "HTTP request");

    std::string response = sock.Receive("HTTP response");
    
    sock.Close();

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
            
            auto const toolResponse = ParseToolCall(assistant_reply);
            if (!toolResponse)
            {
                std::cout << "assistant> " << assistant_reply << std::endl;
                break;
            }

            auto const& tool_result = toolResponse.value();

            std::cout << "tool> ";
            if (assistant_reply.size() > 20)
            {
                std::cout << EscapeJsonString(assistant_reply.substr(0, 20)) << "...";
            }
            else
            {
                std::cout << EscapeJsonString(assistant_reply);
            }
            std::cout << " -> ";
            if (tool_result.size() > 20)
            {
                std::cout << EscapeJsonString(tool_result.substr(0, 20)) << "...";
            }
            else
            {
                std::cout << EscapeJsonString(tool_result);
            }

            conversation_prompt += "Tool call: \"";
            conversation_prompt += EscapeJsonString(assistant_reply);
            conversation_prompt += "\"\nResult: \"";
            conversation_prompt += EscapeJsonString(tool_result);
            conversation_prompt += "\"\nContinue the task.";
        }
        catch (std::exception const& error)
        {
            std::cerr << "agent error: " << error.what() << std::endl;
            return 1;
        }
    }

    return 0;
}
