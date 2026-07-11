
#include "FileUtilities.h"
#include "NetworkUtilities.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"
#include "Tools/edit_file_lines.h"
#include "Tools/glob.h"
#include "Tools/grep.h"
#include "Tools/read_file_chunk.h"
#include "Tools/read_file.h"
#include "Tools/write_file.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using json = nlohmann::json;

namespace
{

constexpr ToolDefinition AllTools[] =
{
    //read_file,
    glob,
    grep,
    read_file_chunk,
    edit_file_lines,
    write_file,
};

struct ConversationMessage
{
    std::string   role      {};
    std::string   content   {};
    std::string   toolCallId{};
    json::array_t toolCalls {};
};

json BuildPayload(std::span<ConversationMessage> const conversationHistory = {})
{
    json::array_t messages{};

    for (auto& message : conversationHistory)
    {
        json messageJson = {
            { "role"   , message.role    },
            { "content", message.content },
        };

        if (!message.toolCallId.empty())
        {
            messageJson["tool_call_id"] = message.toolCallId;
        }

        if (!message.toolCalls.empty())
        {
            messageJson["tool_calls"] = message.toolCalls;
        }

        messages.push_back(std::move(messageJson));
    }

    return json{
        { "model", "gemma-4" },
        { "messages", std::move(messages) },
        { "tools", BuildPayloadToolDefinitions(AllTools) },
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

std::string to_string(ResponseFinishReason const reason)
{
    switch (reason)
    {
    case ResponseFinishReason::stop          : return "stop";
    case ResponseFinishReason::length        : return "length";
    case ResponseFinishReason::tool_calls    : return "tool_calls";
    case ResponseFinishReason::content_filter: return "content_filter";
    default                                  : return "<unknown ResponseFinishReason>";
    }
}

struct ModelResponse
{
    ResponseFinishReason  reason;
    std::string           reasoning;
    std::string           content;
    std::vector<ToolCall> toolCalls;
};

ModelResponse ExtractModelContent(json const& response)
{
    ModelResponse result;

    if (!response.is_object()) throw std::runtime_error("malformed response: not a JSON object");

    json const& choices = response.at("choices");
    if (!choices.is_array() || choices.empty()) throw std::runtime_error("malformed response: missing choices");

    auto& choice = choices[0];

    auto& finishReason = choice.at("finish_reason").get_ref<std::string const&>();
    if      (finishReason == "stop"          ) result.reason = ResponseFinishReason::stop;
    else if (finishReason == "length"        ) result.reason = ResponseFinishReason::length;
    else if (finishReason == "tool_calls"    ) result.reason = ResponseFinishReason::tool_calls;
    else if (finishReason == "content_filter") result.reason = ResponseFinishReason::content_filter;
    else
    {
        throw std::runtime_error(std::string("malformed response: unknown finish_reason: ") + finishReason);
    }

    if (result.reason != ResponseFinishReason::stop && result.reason != ResponseFinishReason::tool_calls)
    {
        std::cout << "Response:\n" << Utf8ToSystemEncoding(response.dump(2)) << std::endl << std::endl;
    }

    json const& message = choice.at("message");
    if (!message.is_object() || message.empty()) throw std::runtime_error("malformed response: missing message");

    result.content   = message.value<std::string>("content"          , {});
    result.reasoning = message.value<std::string>("reasoning_content", {});
    result.toolCalls = ParseToolCalls(message.value<json::array_t>("tool_calls", {}));

    return result;
}

std::string HttpPost(Endpoint const& endpoint, json const& payload)
{
    return HttpPost(endpoint, "application/json", payload.dump());
}

std::string BuildToolResponseMessage(std::string_view const toolName, std::string_view const toolResult)
{
    json response = {
        { "result", std::string(toolResult) },
    };

    if (!toolName.empty())
    {
        response["tool"] = std::string(toolName);
    }

    return response.dump();
}

std::string GenerateLogId()
{
    static constexpr char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";

    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::uniform_int_distribution<int> dist(0, static_cast<int>(sizeof(kAlphabet) - 2));

    std::string id;
    id.reserve(8);
    for (int i = 0; i < 8; ++i)
    {
        id.push_back(kAlphabet[dist(rng)]);
    }

    return id;
}

std::filesystem::path const& GetLogsDirectory()
{
    static auto const path = GetExecutableDirectory() / "logs";
    if (!std::filesystem::exists(path))
    {
        std::filesystem::create_directory(path);
    }
    return path;
}

struct Log
{
    std::string id = GenerateLogId();
    int         sequence = 0;
};

std::filesystem::path BuildLogPath(std::string_view const logId, int sequence, std::string_view const name)
{
    std::ostringstream filename;
    filename << logId << '_' << std::setw(3) << std::setfill('0') << sequence << '_' << name << ".json";
    return GetLogsDirectory() / filename.str();
}

void WriteLogFile(Log& log, std::string_view const name, std::string_view const content)
{
    auto const logPath = BuildLogPath(log.id, log.sequence, name);
    if (auto output = std::ofstream(logPath, std::ios::binary | std::ios::trunc))
    {
        output << content;
    }
    else
    {
        throw std::runtime_error(std::string("unable to write log file: ") + logPath.string());
    }
}

}

class ChatSession
{
public:
    ChatSession(Endpoint endpointDescriptor, ToolsRuntimeContext toolContext, std::string systemPrompt)
        : m_endpointDescriptor{ endpointDescriptor     }
        , m_toolContext       { std::move(toolContext) }
    {
        m_conversationHistory.push_back({ .role = "system", .content = std::move(systemPrompt) });

        std::cout << "log id: " << m_log.id << std::endl;
    }

    std::string Prompt(std::string prompt, int maxTurns)
    {
        if (prompt.empty())
        {
            return {};
        }

        m_conversationHistory.push_back({ .role = "user", .content = std::move(prompt) });

        for (int turn = 0; turn < maxTurns; ++turn, ++m_log.sequence)
        {
            std::cout << std::endl << "===============================\nTurn " << turn << std::endl << std::endl;
            try
            {
                json const payload = BuildPayload(m_conversationHistory);
                WriteLogFile(m_log, "request", payload.dump(2));

                std::string const responseBody = HttpPost(m_endpointDescriptor, payload);

                json const response = json::parse(responseBody.begin(), responseBody.end(), nullptr, false);
                WriteLogFile(m_log, "response", response.dump(2));

                auto modelResponse = ExtractModelContent(response);

                // Show the reasoning stream from the model.
                if (!modelResponse.reasoning.empty())
                {
                    std::cout << "reasoning> " << Utf8ToSystemEncoding(modelResponse.reasoning) << std::endl << std::endl;
                }

                if (!modelResponse.content.empty())
                {
                    // See if we got an old-school textual tool call.
                    auto toolResponse = ParseToolCall(modelResponse.content, m_toolContext, AllTools);
                    if (toolResponse)
                    {
                        auto& toolResult = toolResponse.value();

                        std::cout << "tool> " << Utf8ToSystemEncoding(OneLine(modelResponse.content, 20)) << " -> " << Utf8ToSystemEncoding(OneLine(toolResult, 20)) << std::endl << std::endl;

                        m_conversationHistory.push_back({
                            .role    = "user",
                            .content = toolResult,
                        });
                        m_conversationHistory.push_back({
                            .role    = "assistant",
                            .content = std::move(modelResponse.content),
                        });
                        continue;
                    }
                    else
                    {
                        std::cout << "assistant> " << Utf8ToSystemEncoding(modelResponse.content) << std::endl << std::endl;
                    }
                }

                if (modelResponse.reason == ResponseFinishReason::stop)
                {
                    m_conversationHistory.push_back({
                        .role      = "assistant",
                        .content   = modelResponse.content,
                    });
                    return std::move(modelResponse.content);
                }
                else if (modelResponse.reason == ResponseFinishReason::tool_calls)
                {
                    if (modelResponse.toolCalls.empty())
                    {
                        std::cerr << "The model said it was calling tools, but we didn't find any tool to call" << std::endl;
                        throw std::runtime_error("The model said it was calling tools, but we didn't find any tool to call");
                    }

                    ConversationMessage assistantMessage{
                        .role      = "assistant",
                        .content   = std::move(modelResponse.content),
                        .toolCalls = json::array(),
                    };
                    for (auto const& toolCall : modelResponse.toolCalls)
                    {
                        assistantMessage.toolCalls.push_back({
                            { "type", "function" },
                            { "function", {
                                { "name"     , toolCall.name             },
                                { "arguments", toolCall.arguments.dump() },
                            }},
                            { "id", toolCall.id },
                        });
                    }
                    m_conversationHistory.push_back(std::move(assistantMessage));

                    for (auto const& toolCall : modelResponse.toolCalls)
                    {
                        std::string toolResult = CallTool(toolCall.name, toolCall.arguments, m_toolContext, AllTools);
                        std::cout << "tool> " << toolCall.name << '(' << Utf8ToSystemEncoding(OneLine(toolCall.arguments.dump(), 20)) << ") -> " << Utf8ToSystemEncoding(OneLine(toolResult, 20)) << std::endl << std::endl;
                        m_conversationHistory.push_back({
                            .role       = "tool",
                            .content    = BuildToolResponseMessage(toolCall.name, toolResult),
                            .toolCallId = toolCall.id,
                        });
                    }
                }
                else
                {
                    std::cerr << "TODO: stop reason " << Utf8ToSystemEncoding(to_string(modelResponse.reason));
                    throw std::runtime_error(std::string("TODO: stop reason ") + to_string(modelResponse.reason));
                }
            }
            catch (std::exception const& error)
            {
                std::cerr << "agent error: " << Utf8ToSystemEncoding(error.what()) << std::endl;
                throw;
            }
        }

        return "The model took too many turns";
    }

private:
    Endpoint                         m_endpointDescriptor;
    ToolsRuntimeContext                      m_toolContext;
    std::vector<ConversationMessage> m_conversationHistory;
    Log                              m_log;
};

int main(int argc, char** argv)
{
    //ConfigureConsoleForUtf8();

    std::string endpoint = "http://127.0.0.1:8080/v1/chat/completions";
    std::string prompt;
    int max_turns = 10;

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
        prompt = "Tell me about yourself.";
    }

    ToolsRuntimeContext toolContext{ .fs{ std::filesystem::current_path() } };

    ChatSession session{ ParseEndpoint(endpoint), toolContext, RawReadTextFile(GetExecutableDirectory() / "data" / "SystemPrompt.txt") };

    session.Prompt(prompt, max_turns);

    return 0;
}
