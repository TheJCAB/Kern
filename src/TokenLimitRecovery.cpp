#include "TokenLimitRecovery.h"

#include <print>
#include <string>
#include <utility>

void TokenLimitRecovery::OnProductiveTurn()
{
    m_retries = 0;
    m_messages.clear();
}

void TokenLimitRecovery::OnCutOff(std::string_view reasoning, std::string_view content, unsigned maxTokens)
{
    // Token cap hit mid-thought: for one request only, show the model its partial output and tell it to act.
    // The replacement response is what ends up in the history.
    ++m_retries;
    std::println("Response hit the token limit after {} characters of reasoning and {} characters of content; asking the model to wrap up (retry {}/{})",
        reasoning.size(), content.size(), m_retries, MaxRetries);
    std::string partial;
    if (!reasoning.empty())
    {
        partial += "[Your reasoning so far, cut off by the output token limit. It is carried over so that you do not have to spend your budget repeating it, and it is settled: do not restate, revise or re-derive any of it.]\n"
                + std::string(reasoning)
                + "\n[End of earlier reasoning]\n";
    }
    if (!content.empty())
    {
        partial += "[Partial reply]\n" + std::string(content) + "\n[End of partial reply]\n";
    }
    if (partial.empty())
    {
        partial = "(there was no output before the token limit was reached)";
    }
    m_messages.push_back({
        .role    = "assistant",
        .content = std::move(partial),
    });

    std::string const budget = maxTokens == 0 ? std::string("a fixed number of tokens") : std::to_string(maxTokens) + " tokens";
    std::string nudge;
    if (m_retries == 1)
    {
        // First retry: explain the situation, name the budget and make the failure mode explicit.
        nudge = "Your previous response was cut off because it reached the output token limit before it produced any tool call or answer. "
                "This is a continuation of the same task, not its completion. The whole budget, " + budget + ", applies again to this response, "
                "and the reasoning above was carried over so that you do not have to spend tokens repeating it. "
                "Treat everything above as settled: do not restate, revise or re-derive it. "
                "Spend a short amount of thought here, a paragraph and not a plan, and then act: make the next tool call now. "
                "If the whole task does not fit, do the first part completely and report the rest. "
                "A response that contains only reasoning is a failure: the retry is spent, the session ends and the work above is lost.";
    }
    else
    {
        // Second retry: no more analysis, one action.
        nudge = "Your previous response was cut off again without producing a tool call or an answer. "
                "Stop analysing and respond now, with appropriate tool calls or just a final response, from the plan you already settled on above; "
                "if that plan is incomplete, call the tool with what you have. Keep any reasoning to a single short paragraph. "
                "This is the last retry: another reasoning-only response ends the session and discards all of the work above.";
    }
    m_messages.push_back({
        .role    = "user",
        .content = std::move(nudge),
    });
}
