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

json BuildChatPayload(std::vector<ConversationMessage> const& history,
                      std::vector<ConversationMessage> const& transientMessages,
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
