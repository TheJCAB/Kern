#pragma once

#include "Conversation.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Recovery policy for a model request that failed outright (a dropped connection, a non-2xx answer, a malformed
// response) rather than producing an answer: the failure is reported back to the model so that it can try again, for a
// bounded number of consecutive failures, after which the session reports the failure itself.
//
// The failed request leaves nothing in the conversation history, so the report is shown to the model for one request
// only and never enters the conversation: the next request sees the conversation exactly as it was plus the report.
// A turn that does produce work resets the escalation, so a long session can survive several independent failures.
class ErrorRecovery
{
public:
    // How many failed requests in a row the session tolerates before it gives up.
    static constexpr unsigned MaxRetries = 3;

    // Messages to append to the next request only; empty until the first failure and after a productive turn.
    std::vector<ConversationMessage> const& Messages() const { return m_messages; }

    // Failures spent so far since the last productive turn.
    unsigned Retries() const { return m_retries; }

    // True once the consecutive failures have spent every retry, which is when the session must give up.
    bool Exhausted() const { return m_retries >= MaxRetries; }

    // A turn that produced a tool call or an answer resets the escalation to the first retry.
    void OnProductiveTurn();

    // Prepares the next request after a failed one: reports the failure to the model with wording that depends on how
    // many retries are left, and prints the situation on the console. `diagnostic` is the failure as the transport
    // described it; it is put on one line and truncated to `maxDiagnosticChars`.
    void OnFailure(std::string_view diagnostic, std::size_t maxDiagnosticChars = 500);

private:
    std::vector<ConversationMessage> m_messages;
    unsigned                         m_retries = 0;
};
