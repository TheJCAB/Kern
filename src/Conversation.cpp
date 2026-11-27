#include "Conversation.h"

#include <utility>

ConversationMessage MakeToolResultMessage(std::string toolCallId, std::string_view result)
{
    return ConversationMessage{
        .role       = "tool",
        .content    = std::string(result),
        .toolCallId = std::move(toolCallId),
    };
}

ConversationMessage MakeAssistantToolCallsMessage(std::string_view content, std::span<ToolCall const> toolCalls)
{
    json::array_t toolCallsJson{};
    toolCallsJson.reserve(toolCalls.size());

    // The arguments go out as JSON text, the shape an OpenAI-style endpoint expects, not as an object.
    for (auto const& toolCall : toolCalls)
    {
        toolCallsJson.push_back(json{
            { "id"      , toolCall.id   },
            { "type"    , "function"    },
            { "function", json{
                { "name"     , toolCall.name },
                { "arguments", toolCall.arguments.is_discarded() ? json::object().dump() : toolCall.arguments.dump() },
            } },
        });
    }

    return ConversationMessage{
        .role      = "assistant",
        .content   = std::string(content),
        .toolCalls = std::move(toolCallsJson),
    };
}

json BuildChatPayload(std::span<ConversationMessage const> const history,
                      std::span<ConversationMessage const> const transientMessages,
                      ChatPayloadOptions const& options)
{
    json::array_t messages{};

    auto appendMessage = [&](ConversationMessage const& message)
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
    };

    for (auto const& message : history)
    {
        appendMessage(message);
    }
    for (auto const& message : transientMessages)
    {
        appendMessage(message);
    }

    // The decoding parameters are deliberately hard-coded: temperature, top_p and n are left at their defaults.
    json result{
        { "tools", BuildPayloadToolDefinitions(options.tools) },
        { "stream", true },
        //{ "temperature", 0.7 },
        //{ "top_p", 1.0 },
        //{ "n", 1 },
        { "messages", std::move(messages) },
    };
    if (!options.modelName.empty())
    {
        result["model"] = options.modelName;
    }
    if (options.maxTokens != 0)
    {
        result["max_tokens"] = options.maxTokens;
    }
    return result;
}

json BuildPromptFilesJson(std::span<PromptFile const> promptFiles)
{
    json::array_t result;
    result.reserve(promptFiles.size());
    for (PromptFile const& promptFile : promptFiles)
    {
        result.push_back(json{
            { "filename", promptFile.filename },
            { "content", promptFile.content },
        });
    }
    return result;
}

ConversationMessage MakePromptMessage(std::string_view prompt, std::span<PromptFile const> promptFiles)
{
    std::string content{ prompt };
    if (!promptFiles.empty())
    {
        content += "\n\nPrompt files:\n";
        content += BuildPromptFilesJson(promptFiles).dump(2);
    }
    return ConversationMessage{
        .role = "user",
        .content = std::move(content),
    };
}
