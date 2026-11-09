#pragma once

#include "Conversation.h"

#include <string_view>
#include <vector>

// Recovery policy for a response that stopped because it hit the output token limit: the model is shown its own
// cut-off output and told to act, for a bounded number of retries, after which the session gives up.
//
// The messages it produces are shown to the model for one request only and never enter the conversation. A turn
// that does produce work resets the escalation, so a long session can recover from several cut-offs.
class TokenLimitRecovery
{
public:
    // How many nudged retries a cut-off response gets before the session reports failure.
    static constexpr unsigned MaxRetries = 2;

    // Messages to append to the next request only; empty until the first cut-off and after a productive turn.
    std::vector<ConversationMessage> const& Messages() const { return m_messages; }

    // Retries used so far since the last productive turn.
    unsigned Retries() const { return m_retries; }

    // True once a cut-off response has spent every retry, which is when the session must give up.
    bool Exhausted() const { return m_retries >= MaxRetries; }

    // A turn that produced a tool call or a final answer resets the escalation to the polite first retry.
    void OnProductiveTurn();

    // Prepares the next request after a cut-off response: carries over the partial output, appends a nudge whose
    // wording depends on how many retries are left, and reports the situation on the console. `maxTokens` is the
    // configured output cap, or 0 when the request sends none.
    void OnCutOff(std::string_view reasoning, std::string_view content, unsigned maxTokens);

private:
    std::vector<ConversationMessage> m_messages;
    unsigned                         m_retries = 0;
};
