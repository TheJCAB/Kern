#include "TurnLimitRecovery.h"
#include "SubagentSessions.h"

#include "TestUtilities.h"

#include <cstddef>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace
{

bool Contains(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

int main()
{
    std::println("Test the turn limit has one agreed error code");
    {
        ExpectEqual(std::string(TurnLimitErrorCode), std::string("too_many_turns"),
            "the error code is the name both the session and the REPL use");
    }

    std::println("Test the policy always grants at least one turn");
    {
        Expect(TurnLimitRecovery::WrapUpTurns >= 1u, "the policy grants at least one report turn");
        Expect(TurnLimitRecovery::DefaultGrantTurns >= 1u, "a follow-up prompt always grants at least one turn");
    }

    std::println("Test a fresh recovery has no messages and no turns pending");
    {
        TurnLimitRecovery recovery;
        Expect(recovery.Messages().empty(), "a fresh recovery carries no messages");
        ExpectEqual(recovery.PendingWrapUpTurns(), 0u, "a fresh recovery has no extra turns pending");
        Expect(!recovery.WrapUpPending(), "a fresh recovery owes no report");
    }

    std::println("Test offering the wrap-up names the spent budget and forbids further work");
    {
        TurnLimitRecovery recovery;
        recovery.OfferWrapUp(50u, 50u);

        ExpectEqual(recovery.Messages().size(), std::size_t{ 1 }, "the wrap-up is a single instruction");
        ExpectEqual(recovery.Messages().at(0).role, std::string("user"), "the instruction is the user message");
        Expect(Contains(recovery.Messages().at(0).content, "This is your last turn"),
            "the instruction says this is the last turn");
        Expect(Contains(recovery.Messages().at(0).content, "used all 50 turns"), "the instruction names the budget");
        Expect(Contains(recovery.Messages().at(0).content, "and 50 have been spent"),
            "the instruction names the turns spent");
        Expect(Contains(recovery.Messages().at(0).content, "No further tool call of yours will be executed."),
            "the instruction forbids calling a tool");
        Expect(Contains(recovery.Messages().at(0).content, "Do not start new work, do not call any tool"),
            "the instruction forbids starting new work");
        Expect(Contains(recovery.Messages().at(0).content, "Write your final response now"),
            "the instruction asks for the final report");

        ExpectEqual(recovery.PendingWrapUpTurns(), TurnLimitRecovery::WrapUpTurns,
            "the wrap-up grants the report turns");
        Expect(recovery.WrapUpPending(), "the wrap-up leaves a report owed");
    }

    std::println("Test the instruction names the spent turns even when they differ from the budget");
    {
        TurnLimitRecovery recovery;
        recovery.OfferWrapUp(37u, 50u);

        ExpectEqual(recovery.Messages().size(), std::size_t{ 1 }, "the wrap-up is a single instruction");
        ExpectEqual(recovery.Messages().at(0).role, std::string("user"), "the instruction is the user message");
        Expect(Contains(recovery.Messages().at(0).content, "used all 50 turns"), "the instruction names the budget");
        Expect(Contains(recovery.Messages().at(0).content, "and 37 have been spent"),
            "the instruction names the turns actually spent");
        Expect(Contains(recovery.Messages().at(0).content, "This is your last turn"),
            "the instruction still says this is the last turn");
    }

    std::println("Test spending the report turn clears the instruction and the pending turns");
    {
        TurnLimitRecovery recovery;
        recovery.OfferWrapUp(50u, 50u);
        Expect(recovery.WrapUpPending(), "the report is owed before the turn is spent");

        recovery.OnWrapUpTurn();

        Expect(recovery.Messages().empty(), "the report turn drops the transient instruction");
        ExpectEqual(recovery.PendingWrapUpTurns(), 0u, "the report turn spends the pending turns");
        Expect(!recovery.WrapUpPending(), "no report is owed once the turn is spent");
    }

    std::println("All tests passed!");

    std::println("the subagent turn allowances and refusals are unambiguous");
    {
        // A resume is only worth offering when it can add a turn the first call could not: the lifetime allowance has
        // to be more than one call's budget, and at least one subagent must be allowed to stay live for a resume.
        Expect(SubagentLifetimeMaxTurns > SubagentMaxTurns, "a subagent lifetime must allow more turns than one call");
        Expect(SubagentMaxTurns > 0, "a subagent call must be granted at least one turn");
        Expect(MaxLiveSubagentSessions > 0, "at least one subagent session must be allowed to stay live");

        // The orchestrator tells these refusals apart from one another and from a stoppage at the turn limit, so
        // each one has to be its own non-empty string and none may collide with the turn-limit error code.
        std::vector<std::string_view> const codes{
            UnknownSubagentSessionErrorCode,
            SubagentSessionBusyErrorCode,
            SubagentSessionLimitErrorCode,
            SubagentTurnCapErrorCode,
        };

        for (std::size_t index = 0; index < codes.size(); ++index)
        {
            Expect(!codes[index].empty(), "every subagent refusal must have an error code");
            Expect(codes[index] != TurnLimitErrorCode, "a refusal must not reuse the turn-limit error code");

            for (std::size_t other = index + 1; other < codes.size(); ++other)
            {
                Expect(codes[index] != codes[other], "two subagent refusals must not share one error code");
            }
        }
    }

    return 0;
}
