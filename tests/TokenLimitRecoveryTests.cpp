#include "TokenLimitRecovery.h"

#include "TestUtilities.h"

#include <print>
#include <string>
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
    std::println("Test a fresh recovery has no messages, no retries and is not exhausted");
    {
        TokenLimitRecovery recovery;
        Expect(recovery.Messages().empty(), "a fresh recovery carries no messages");
        ExpectEqual(recovery.Retries(), 0u, "a fresh recovery has spent no retries");
        Expect(!recovery.Exhausted(), "a fresh recovery is not exhausted");
    }

    std::println("Test the first cut-off carries over the partial output and asks politely");
    {
        TokenLimitRecovery recovery;
        recovery.OnCutOff("half a plan", "half an answer", 4096u);

        ExpectEqual(recovery.Messages().size(), std::size_t{ 2 }, "the first cut-off adds the carried output and a nudge");
        ExpectEqual(recovery.Messages().at(0).role, std::string("assistant"), "the carried output is the assistant message");
        Expect(Contains(recovery.Messages().at(0).content, "half a plan"), "the reasoning is carried over");
        Expect(Contains(recovery.Messages().at(0).content, "[Your reasoning so far, cut off by the output token limit."),
            "the reasoning is wrapped");
        Expect(Contains(recovery.Messages().at(0).content, "[End of earlier reasoning]"), "the reasoning wrapper is closed");
        Expect(Contains(recovery.Messages().at(0).content, "half an answer"), "the content is carried over");
        Expect(Contains(recovery.Messages().at(0).content, "[Partial reply]"), "the content wrapper is opened");
        Expect(Contains(recovery.Messages().at(0).content, "[End of partial reply]"), "the content wrapper is closed");

        ExpectEqual(recovery.Messages().at(1).role, std::string("user"), "the nudge is the user message");
        Expect(Contains(recovery.Messages().at(1).content, "4096 tokens"), "the first nudge names the configured budget");
        Expect(Contains(recovery.Messages().at(1).content, "do not restate, revise or re-derive it"),
            "the first nudge is the long explanatory retry");
        Expect(!Contains(recovery.Messages().at(1).content, "Stop analysing and respond now"),
            "the first nudge is not the short last-retry text");

        ExpectEqual(recovery.Retries(), 1u, "the first cut-off spends one retry");
        Expect(!recovery.Exhausted(), "one retry is not exhausted");
    }

    std::println("Test the second cut-off appends the short last-retry text and exhausts the policy");
    {
        TokenLimitRecovery recovery;
        recovery.OnCutOff("first reasoning", "first content", 4096u);
        recovery.OnCutOff("second reasoning", "second content", 4096u);

        ExpectEqual(recovery.Messages().size(), std::size_t{ 4 }, "the second cut-off appends two more messages");
        ExpectEqual(recovery.Messages().at(2).role, std::string("assistant"), "the second carried output is an assistant message");
        Expect(Contains(recovery.Messages().at(2).content, "second reasoning"), "the second reasoning is carried over");
        Expect(Contains(recovery.Messages().at(2).content, "second content"), "the second content is carried over");
        ExpectEqual(recovery.Messages().at(3).role, std::string("user"), "the second nudge is a user message");
        Expect(Contains(recovery.Messages().at(3).content, "Stop analysing and respond now"),
            "the second nudge is the short last-retry text");
        Expect(Contains(recovery.Messages().at(3).content, "This is the last retry"),
            "the second nudge says it is the last retry");

        ExpectEqual(recovery.Retries(), 2u, "the second cut-off spends the second retry");
        Expect(recovery.Exhausted(), "two retries exhaust the policy");
    }

    std::println("Test a request without an output cap names no token count");
    {
        TokenLimitRecovery recovery;
        recovery.OnCutOff({}, {}, 0u);

        Expect(Contains(recovery.Messages().at(1).content, "a fixed number of tokens"),
            "the nudge falls back to the fixed-budget wording");
        Expect(!Contains(recovery.Messages().at(1).content, "0 tokens"), "the nudge never prints a zero token count");
    }

    std::println("Test a cut-off with no output still carries over a placeholder");
    {
        TokenLimitRecovery recovery;
        recovery.OnCutOff({}, {}, 4096u);

        ExpectEqual(recovery.Messages().size(), std::size_t{ 2 }, "an empty cut-off still produces both messages");
        Expect(Contains(recovery.Messages().at(0).content, "(there was no output before the token limit was reached)"),
            "the placeholder stands in for the missing output");
        Expect(!Contains(recovery.Messages().at(0).content, "[Partial reply]"), "no empty partial reply wrapper is emitted");
    }

    std::println("Test a productive turn resets the escalation");
    {
        TokenLimitRecovery recovery;
        recovery.OnCutOff("reasoning", "content", 4096u);
        recovery.OnCutOff("more reasoning", "more content", 4096u);
        Expect(recovery.Exhausted(), "the escalation is exhausted before the productive turn");

        recovery.OnProductiveTurn();

        Expect(recovery.Messages().empty(), "the productive turn drops the transient messages");
        ExpectEqual(recovery.Retries(), 0u, "the productive turn resets the retry count");
        Expect(!recovery.Exhausted(), "the productive turn makes the policy usable again");

        recovery.OnCutOff("later reasoning", "later content", 4096u);
        Expect(Contains(recovery.Messages().at(1).content, "do not restate, revise or re-derive it"),
            "recovery starts again at the long explanatory retry");
    }

    return 0;
}
