#pragma once

#include "JsonUtilities.h"
#include "SessionRecorder.h"

#include <string_view>

// Builds the description of a turn of the agent loop.
// Upon destruction, whether at the end of its scope or because of an exception,
// writes the turn to the session recorder if it's enabled.
// This ensures turns are recorded reliably even in the presence of errors.
class TurnRecord
{
public:
    TurnRecord(SessionRecorder& recorder, unsigned turn);

    TurnRecord(TurnRecord const&)            = delete;
    TurnRecord& operator=(TurnRecord const&) = delete;

    // Writes the accumulated turn, if the recorder is enabled.
    ~TurnRecord() noexcept;

    // Records the "usage" object as sent by the service; anything that is not an object is ignored.
    void SetUsage(json const& usage);

    // Round-trip time of the request in milliseconds.
    void SetResponseMilliseconds(double milliseconds);

    // Why the response stopped; an empty reason is ignored.
    void SetFinishReason(std::string_view reason);

    // Text the assistant produced. An empty text is recorded only once a finish reason exists, so that a turn
    // that stopped for another reason does not claim the assistant said nothing.
    void SetAssistantText(std::string_view text);

    // Appends one requested tool call to the "toolCalls" array, creating it on first use.
    void AddToolCall(json const& call);

    // Appends one tool result to "toolResults", keeping the full length and a truncated copy of large results,
    // and lifting a JSON `error` member into the entry when the result parses as an object that has one.
    void AddToolResult(std::string_view name, std::string const& result);

private:
    SessionRecorder& m_recorder;
    json             m_turn;
};
