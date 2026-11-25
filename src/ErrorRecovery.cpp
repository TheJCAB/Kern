#include "ErrorRecovery.h"

#include "StringUtilities.h"

#include <print>
#include <string>
#include <utility>

void ErrorRecovery::OnProductiveTurn()
{
    m_retries = 0;
    m_messages.clear();
}

void ErrorRecovery::OnFailure(std::string_view diagnostic, std::size_t maxDiagnosticChars)
{
    // A failed request leaves nothing behind, so the report describes only the request that failed immediately before
    // it: it replaces any earlier report rather than piling up across the consecutive failures.
    ++m_retries;
    m_messages.clear();
    std::println("Model request failed; asking the model to try again (failure {}/{} of this turn)", m_retries, MaxRetries);
    std::string detail = OneLine(diagnostic, maxDiagnosticChars);
    if (detail.empty())
    {
        detail = "(the service reported no detail)";
    }

    std::string const common = "The previous request to the model service failed: [" + detail
                             + "]. That request produced nothing that was recorded, so the conversation is exactly as it "
                               "was before it, and the tool call or the answer you were about to produce still has to be "
                               "produced now. ";
    std::string nudge;
    if (m_retries == 1)
    {
        // First failure: explain the situation plainly, and keep the model from spending this request re-planning.
        nudge = common
              + "Keep any reasoning to a short paragraph and then act: make the tool call you were already about to make.";
    }
    else if (m_retries < MaxRetries)
    {
        // A further failure with retries still in hand: the identical request is expected to fail again, so vary it.
        nudge = common
              + "This is failure " + std::to_string(m_retries) + " of " + std::to_string(MaxRetries)
              + " in a row. Asking for the identical thing again is likely to fail again, so change what you ask for: read "
                "a smaller part of the file, touch a different file, or delegate a narrower piece of the work. If you "
                "cannot vary it, report the work done so far instead.";
    }
    else
    {
        // The last failure this session absorbs: one smallest possible call, or the final response, before the next one.
        nudge = common
              + "This is failure " + std::to_string(MaxRetries) + " of " + std::to_string(MaxRetries)
              + ", and it is the last one this session will absorb: the next failure ends the session and discards the "
                "work. Either make the single smallest possible tool call, or write your final response now.";
    }
    m_messages.push_back({
        .role    = "user",
        .content = std::move(nudge),
    });
}
