
#include "FileUtilities.h"
#include "NetworkUtilities.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using json = nlohmann::json;

namespace
{

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

constexpr ToolDefinition AllTools[] =
{
    { "read_file" , "Read the contents of a file.", ReadFileToolParameters , ReadFileTool  },
    { "write_file", "Write content to a file."    , WriteFileToolParameters, WriteFileTool },
};

struct SessionRound
{
    std::string assistant;
    std::string user;
};

json BuildPayload(std::string_view const systemPrompt, std::string_view const initialUserPrompt, std::span<SessionRound> const conversationHistory = {})
{
    auto messages = json::array({
        { { "role", "system" }, { "content", systemPrompt } },
        { { "role", "user"   }, { "content", initialUserPrompt   } },
    });

    for (auto& round : conversationHistory)
    {
        messages.push_back({ { "role", "assistant" }, { "content", round.assistant } });
        messages.push_back({ { "role", "user"      }, { "content", round.user      } });
    }

    return json{
        { "model", "gemma-4" },
        { "messages", std::move(messages) },
        //{ "max_tokens", 1024 },
        //{ "temperature", 0.7 },
        //{ "top_p", 1.0 },
        //{ "n", 1 },
        //{ "stream", false },
        //{ "stop", json::array({ "\n\n" }),
    };
}

enum class ResponseFinishReason
{
    stop          ,
    length        ,
    tool_calls    ,
    content_filter,
};

struct ModelResponse
{
    ResponseFinishReason reason;
    std::string          reasoning;
    std::string          content;
};

ModelResponse ExtractModelContent(std::string_view const response_body)
{
    ModelResponse result;

    json const response = json::parse(response_body.begin(), response_body.end(), nullptr, false);
    if (!response.is_object()) throw std::runtime_error("malformed response: not a JSON object");

    json const& choices = response["choices"];
    if (!choices.is_array() || choices.empty()) throw std::runtime_error("malformed response: missing choices");

    auto& finishReason = choices[0]["finish_reason"].get_ref<std::string const&>();
    if      (finishReason == "stop"          ) result.reason = ResponseFinishReason::stop;
    else if (finishReason == "length"        ) result.reason = ResponseFinishReason::length;
    else if (finishReason == "tool_calls"    ) result.reason = ResponseFinishReason::tool_calls;
    else if (finishReason == "content_filter") result.reason = ResponseFinishReason::content_filter;
    else
    {
        throw std::runtime_error(std::string("malformed response: unknown finish_reason: ") + finishReason);
    }

    json const& message = choices[0]["message"];
    if (!message.is_object() || message.empty()) throw std::runtime_error("malformed response: missing message");

    result.content = message["content"].get<std::string>();

    json const& reasoning = message["reasoning_content"];
    if (reasoning.is_string())
    {
        result.reasoning = reasoning.get<std::string>();
    }

    return result;
}

std::string HttpPost(Endpoint const& endpoint, json const& payload)
{
    return HttpPost(endpoint, "application/json", payload.dump());
}

}

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

    std::string const systemPrompt = ReadTextFile(GetExecutableDirectory() / "data" / "SystemPrompt.txt");
    std::string initialUserPrompt = prompt;

    std::vector<SessionRound> conversationHistory;

    for (int turn = 0; turn < max_turns; ++turn)
    {
        try
        {
            json const payload = BuildPayload(systemPrompt, initialUserPrompt, conversationHistory);
            //printf("Sending request to %s with payload:\n%s\n", endpoint.c_str(), payload.c_str());
            auto const modelResponse = ExtractModelContent(HttpPost(endpointDescriptor, payload));

            if (!modelResponse.reasoning.empty())
            {
                std::cout << "reasoning> " << modelResponse.reasoning << std::endl << std::endl;
            }

            std::string const assistantReply = modelResponse.content;
            
            auto const toolResponse = ParseToolCall(assistantReply, AllTools);
            if (!toolResponse)
            {
                std::cout << "assistant> " << assistantReply << std::endl << std::endl;
                break;
            }

            auto const& toolResult = toolResponse.value();

            std::cout << "tool> ";
            if (assistantReply.size() > 20)
            {
                std::cout << EscapeJsonString(assistantReply.substr(0, 20)) << "...";
            }
            else
            {
                std::cout << EscapeJsonString(assistantReply);
            }
            std::cout << " -> ";
            if (toolResult.size() > 20)
            {
                std::cout << EscapeJsonString(toolResult.substr(0, 20)) << "...";
            }
            else
            {
                std::cout << EscapeJsonString(toolResult);
            }

            conversationHistory.push_back({ assistantReply, toolResult });
        }
        catch (std::exception const& error)
        {
            std::cerr << "agent error: " << error.what() << std::endl;
            return 1;
        }
    }

    return 0;
}
