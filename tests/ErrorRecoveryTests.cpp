#include "ErrorRecovery.h"

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
        ErrorRecovery recovery;
        Expect(recovery.Messages().empty(), "a fresh recovery carries no messages");
        ExpectEqual(recovery.Retries(), 0u, "a fresh recovery has spent no retries");
        Expect(!recovery.Exhausted(), "a fresh recovery is not exhausted");
    }

    std::println("Test the first failure reports the diagnostic and asks the model to act");
    {
        ErrorRecovery recovery;
        recovery.OnFailure("connection dropped", 500u);

        ExpectEqual(recovery.Messages().size(), std::size_t{ 1 }, "the first failure leaves exactly one message");
        ExpectEqual(recovery.Messages().at(0).role, std::string("user"), "the report is the user message");
        Expect(Contains(recovery.Messages().at(0).content, "connection dropped"), "the diagnostic is named");
        Expect(Contains(recovery.Messages().at(0).content, "The previous request to the model service failed"),
            "the report names the failed request");
        Expect(Contains(recovery.Messages().at(0).content, "make the tool call you were already about to make."),
            "the first failure is the plain explanatory report");
        Expect(!Contains(recovery.Messages().at(0).content, "turn limit"), "the report never mentions a turn limit");
        Expect(!Contains(recovery.Messages().at(0).content, "turn budget"), "the report never mentions a turn budget");

        ExpectEqual(recovery.Retries(), 1u, "the first failure spends one retry");
        Expect(!recovery.Exhausted(), "one failure is not exhausted");
    }

    std::println("Test consecutive failures replace the report and escalate its wording");
    {
        ErrorRecovery recovery;
        recovery.OnFailure("first failure", 500u);
        std::string const first = recovery.Messages().at(0).content;

        recovery.OnFailure("second failure", 500u);

        ExpectEqual(recovery.Messages().size(), std::size_t{ 1 }, "the second failure replaces the earlier report");
        Expect(recovery.Messages().at(0).content != first, "the second report is worded differently from the first");
        Expect(Contains(recovery.Messages().at(0).content, "2"), "the second report names the retry count");
        Expect(Contains(recovery.Messages().at(0).content, "This is failure 2 of 3 in a row"),
            "the second report counts the failure against the budget");
        Expect(Contains(recovery.Messages().at(0).content, "Asking for the identical thing again is likely to fail again"),
            "the second report asks the model to vary the request");
        Expect(!Contains(recovery.Messages().at(0).content, "Keep any reasoning to a short paragraph"),
            "the second report is no longer the first-failure wording");

        recovery.OnFailure("third failure", 500u);

        ExpectEqual(recovery.Messages().size(), std::size_t{ 1 }, "the third failure still leaves one message");
        ExpectEqual(recovery.Retries(), ErrorRecovery::MaxRetries, "every retry is spent after MaxRetries failures");
        Expect(recovery.Exhausted(), "the consecutive failures exhaust the policy");
    }

    std::println("Test the last failure carries the strongest instruction");
    {
        ErrorRecovery recovery;
        recovery.OnFailure("first failure", 500u);
        std::string const first = recovery.Messages().at(0).content;

        recovery.OnFailure("second failure", 500u);
        recovery.OnFailure("third failure", 500u);

        Expect(Contains(recovery.Messages().at(0).content, "the last one this session will absorb"),
            "the last failure says it is the last one absorbed");
        Expect(Contains(recovery.Messages().at(0).content, "the next failure ends the session and discards the work"),
            "the last failure names what the next failure costs");
        Expect(Contains(recovery.Messages().at(0).content, "Either make the single smallest possible tool call"),
            "the last failure asks for one smallest call or the final response");
        Expect(!Contains(first, "the last one this session will absorb"),
            "the first failure does not carry the last-failure instruction");
    }

    std::println("Test a long diagnostic is truncated to the character limit");
    {
        ErrorRecovery recovery;
        std::string const diagnostic = "HEADMARKER" + std::string(1990, 'x') + "ENDMARKER";
        recovery.OnFailure(diagnostic, 50u);

        ExpectEqual(recovery.Messages().size(), std::size_t{ 1 }, "a truncated diagnostic still leaves one message");
        Expect(Contains(recovery.Messages().at(0).content, "HEADMARKER"), "the start of the diagnostic survives");
        Expect(!Contains(recovery.Messages().at(0).content, "ENDMARKER"),
            "the end of a diagnostic past the limit is dropped");
    }

    std::println("Test an empty diagnostic yields the placeholder text");
    {
        ErrorRecovery recovery;
        recovery.OnFailure({}, 500u);

        ExpectEqual(recovery.Messages().size(), std::size_t{ 1 }, "an empty diagnostic still leaves one message");
        Expect(Contains(recovery.Messages().at(0).content, "(the service reported no detail)"),
            "the placeholder stands in for the missing detail");
    }

    std::println("Test a productive turn resets the escalation");
    {
        ErrorRecovery recovery;
        recovery.OnFailure("first failure", 500u);
        recovery.OnFailure("second failure", 500u);
        recovery.OnFailure("third failure", 500u);
        Expect(recovery.Exhausted(), "the escalation is exhausted before the productive turn");

        recovery.OnProductiveTurn();

        Expect(recovery.Messages().empty(), "the productive turn drops the transient message");
        ExpectEqual(recovery.Retries(), 0u, "the productive turn resets the retry count");
        Expect(!recovery.Exhausted(), "the productive turn makes the policy usable again");

        recovery.OnFailure("later failure", 500u);
        Expect(Contains(recovery.Messages().at(0).content, "make the tool call you were already about to make."),
            "recovery starts again at the plain explanatory report");
    }

    std::println("All tests passed!");

    return 0;
}
