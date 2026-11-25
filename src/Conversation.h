#pragma once

#include "JsonUtilities.h"
#include "ToolUtilities.h"

#include <span>
#include <string>
#include <string_view>
#include <span>

// One entry of the conversation as the model sees it: a role, its text, and either the id of the tool call this
// message answers or the tool calls this message requests.
struct ConversationMessage
{
    std::string   role      {};
    std::string   content   {};
    std::string   toolCallId{};
    json::array_t toolCalls {};
};

// The message that carries one tool result back to the model, tied to the call by its id.
ConversationMessage MakeToolResultMessage(std::string toolCallId, std::string_view result);

// The assistant turn that requests tool calls, which must precede the results in the conversation.
ConversationMessage MakeAssistantToolCallsMessage(std::string_view content, std::span<ToolCall const> toolCalls);

// What a request needs besides the conversation: the model, its tool schema, the output cap and whether the
// response is streamed.
struct ChatPayloadOptions
{
    std::string                     modelName{};
    std::span<ToolDefinition const> tools    {};
    unsigned                        maxTokens = 0; // 0 = send no limit
};

// Builds the request body: `history` followed by `transientMessages`, which are shown to the model for this
// request only and never become part of the conversation.
json BuildChatPayload(std::span<ConversationMessage const> history,
                      std::span<ConversationMessage const> transientMessages,
                      ChatPayloadOptions const& options);
