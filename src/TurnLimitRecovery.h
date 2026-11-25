#pragma once

#include "Conversation.h"

#include <string_view>
#include <vector>

// The errorCode a session reports when it spends its whole turn budget: the work is not failed, it is paused,
// because granting more turns resumes the very same conversation.
inline constexpr std::string_view TurnLimitErrorCode = "too_many_turns";

// Recovery policy for a session that spent its whole turn budget without producing an answer. Ending a session
// with nothing to show is worse than ending it with a report, so the session gets a bounded number of extra
// turns whose only purpose is to say what was done, what was learned and what is still missing.
//
// The instruction it writes is shown to the model for one request only and never enters the conversation, so the
// report turn sees the same conversation as any other turn plus that instruction.
class TurnLimitRecovery
{
public:
    // How many extra turns the session gets on its own when the budget runs out.
    static constexpr unsigned WrapUpTurns = 1;

    // How many turns a follow-up prompt grants when it names no count.
    static constexpr unsigned DefaultGrantTurns = 20;

    // Messages to append to the next request only; empty until the budget runs out and after a report turn.
    std::vector<ConversationMessage> const& Messages() const { return m_messages; }

    // Extra turns granted beyond the configured budget and not yet spent: the session's turn cap is raised by
    // this much while a report is still owed.
    unsigned PendingWrapUpTurns() const { return m_pending; }

    // True while the model still owes the session a final report.
    bool WrapUpPending() const { return m_pending > 0; }

    // The budget is spent: grant the report turns and write the instruction that names the spent budget.
    void OfferWrapUp(unsigned usedTurns, unsigned maxTurns);

    // A report turn has been spent, so the session is out of turns for good until more are granted.
    void OnWrapUpTurn();

private:
    std::vector<ConversationMessage> m_messages;
    unsigned                         m_pending = 0;
};
