#include "TurnLimitRecovery.h"

#include <print>
#include <string>
#include <utility>

void TurnLimitRecovery::OnWrapUpTurn()
{
    m_pending = 0;
    m_messages.clear();
}

void TurnLimitRecovery::OfferWrapUp(unsigned const usedTurns, unsigned const maxTurns)
{
    // The budget is spent but the session still owes an answer: grant the report turns and write the instruction
    // that names the spent budget, so that the model reports instead of trying to keep working.
    m_pending = WrapUpTurns;
    std::println("Turn budget of {} spent after {} turns; granting {} more turn for a final report",
        maxTurns, usedTurns, WrapUpTurns);
    std::string instruction = "This is your last turn: you have used all " + std::to_string(maxTurns)
                              + " turns this session is allowed, and " + std::to_string(usedTurns)
                              + " have been spent. No further tool call of yours will be executed."
                                " Write your final response now, as a report of the work done so far:"
                                " what you changed or found, with file paths and the exact identifiers involved,"
                                " what is finished, what remains to be done, and anything you could not verify."
                                " Do not start new work, do not call any tool, and do not restate the plan you"
                                " already had."
                                " A response that calls a tool, or that contains no such report, ends the session"
                                " and discards the work above.";
    m_messages.push_back({
        .role    = "user",
        .content = std::move(instruction),
    });
}
