#pragma once

#include <algorithm>
#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

// The turn budget handed to every subagent session started by `research` or `implement` when the call
// names no `turns` argument.
inline constexpr unsigned SubagentMaxTurns = 10;

// A subagent session is resumable, so a run of small resumes could otherwise continue forever: this is
// the hard cap on the turns one subagent session may spend over its whole lifetime, however many resumes
// grant more. Starting a new subagent is the only way to get a fresh allowance.
inline constexpr unsigned SubagentLifetimeMaxTurns = 60;

// Every live subagent session is memory that never shrinks until it is released, so only this many may be
// live at once. A call that would exceed the cap is refused with `subagent_limit` until the orchestrator
// releases one by passing `release` and its `sessionId` to `research` or `implement`.
inline constexpr std::size_t MaxLiveSubagentSessions = 8;

// The errorCodes a subagent call reports when it cannot run at all: they are distinct so that the
// orchestrator can tell a stale id from a same-turn duplicate from a spent allowance from a full registry.
inline constexpr std::string_view UnknownSubagentSessionErrorCode = "unknown_sessionId";
inline constexpr std::string_view SubagentSessionBusyErrorCode    = "session_busy";
inline constexpr std::string_view SubagentSessionLimitErrorCode   = "subagent_limit";
inline constexpr std::string_view SubagentTurnCapErrorCode        = "turn_cap";

// What running one prompt against one live subagent session produced.
struct SubagentTurnOutcome
{
    bool        ok = false;
    std::string errorCode;
    std::string content;
    // Turns spent over the whole lifetime of the session, never reset by a resume.
    unsigned    turnsUsed = 0;
    bool        stoppedAtTurnLimit = false;
};

// One live subagent session: everything the registry needs from it, so that the registry, the tools and
// the tests never have to know what a `Session` is. `Run` continues the SAME conversation and the SAME
// record, so a resumed subagent appends to its history instead of starting a new one.
class LiveSubagentSession
{
public:
    LiveSubagentSession()                                  = default;
    LiveSubagentSession(LiveSubagentSession const&)        = delete;
    LiveSubagentSession& operator=(LiveSubagentSession const&) = delete;
    LiveSubagentSession(LiveSubagentSession&&)             = delete;
    LiveSubagentSession& operator=(LiveSubagentSession&&)  = delete;
    virtual ~LiveSubagentSession()                         = default;

    // The id the session's recorder assigned: the key it is resumed and released by.
    [[nodiscard]] virtual std::string const& Id() const = 0;

    // Runs `prompt` against the existing history and appends to it.
    virtual SubagentTurnOutcome Run(std::string_view prompt) = 0;

    // Adds `extraTurns` on top of the session's own turn budget; the lifetime cap is enforced by the
    // registry, not by the session.
    virtual void GrantTurns(unsigned extraTurns) = 0;
};

// What one call to `research` or `implement` returns: the identity to resume with, the answer, and the
// budget bookkeeping the orchestrator needs to decide whether resuming is worth it.
struct SubagentRunResult
{
    std::string sessionId;
    bool        ok = false;
    std::string errorCode;
    std::string content;
    // Spent and granted over the whole lifetime of the session: `turnsUsed` never decreases and
    // `turnsAllowed` never shrinks, across any number of resumes.
    unsigned    turnsUsed = 0;
    unsigned    turnsAllowed = 0;
    bool        stoppedAtTurnLimit = false;
};

// A call that could not run at all, reported to the model as a tool error.
struct SubagentError
{
    std::string_view errorCode;
    std::string      message;
};

// Holds the live subagent sessions, keyed by the id their recorder assigned. A session lives until it is
// released or until the main session ends, because resuming it is the only way to spend more turns on work
// that already read the files it needed.
class SubagentSessions
{
public:
    // Builds one live session with the given turn budget. Only called when a call starts a new subagent,
    // never when it resumes one.
    using Factory = std::function<std::unique_ptr<LiveSubagentSession>(unsigned maxTurns)>;

    // Runs `prompt` against the live session named by `resumeSessionId` after granting it `turns` more, or
    // against a newly created session when no id is given. `parentSessionId` and `turnEpoch` identify the
    // turn the call came from, so that a second resume of one session in the same turn is refused instead
    // of interleaving two writes into one conversation history.
    std::expected<SubagentRunResult, SubagentError> Run(Factory const&                  factory,
                                                        std::string_view                parentSessionId,
                                                        unsigned                        turns,
                                                        std::optional<std::string_view> resumeSessionId,
                                                        std::string_view                prompt,
                                                        unsigned long long              turnEpoch)
    {
        // A call that spends no turn cannot make progress, so it is a caller bug and not a stopped run.
        if (turns == 0)
        {
            return std::unexpected(SubagentError{ .errorCode = "invalid_argument",
                                                  .message   = "a subagent call needs at least one turn" });
        }

        Entry* entry = nullptr;

        if (!resumeSessionId)
        {
            // The registry only ever grows until something is released, so the cap is what keeps the process
            // bounded: refusing the call makes the orchestrator choose which finished subagent to forget
            // rather than letting it accumulate silently.
            if (m_live.size() >= MaxLiveSubagentSessions)
            {
                return std::unexpected(SubagentError{
                    .errorCode = SubagentSessionLimitErrorCode,
                    .message   = std::format("already {} live subagent sessions and the cap is {}: each live "
                                             "subagent session holds memory that is never freed until it is "
                                             "released, so pass the `sessionId` of one you are done with and "
                                             "`release` to `research` or `implement`",
                                             m_live.size(),
                                             MaxLiveSubagentSessions)});
            }

            // A new session may not be born with more than it could ever spend, so the call's turns are clipped
            // to the lifetime allowance before the session is built.
            unsigned const                       budget  = std::min(turns, SubagentLifetimeMaxTurns);
            std::unique_ptr<LiveSubagentSession> session = factory(budget);
            if (!session)
            {
                return std::unexpected(SubagentError{ .errorCode = "invalid_argument",
                                                      .message   = "the subagent factory returned no session" });
            }

            std::string id      = session->Id();
            Entry&      started = m_live[id];
            started.session      = std::move(session);
            started.turnsAllowed = budget;
            entry                = &started;
        }
        else
        {
            auto const found = m_live.find(std::string(*resumeSessionId));
            if (found == m_live.end())
            {
                // Resuming an id that is not live can only mean the orchestrator kept one past its release, and
                // answering it with a fresh session would silently throw the old work away.
                return std::unexpected(SubagentError{
                    .errorCode = UnknownSubagentSessionErrorCode,
                    .message   = std::format("no live subagent session is named '{}': it never started or it was "
                                             "already released, so start a new one instead of resuming it",
                                             *resumeSessionId)});
            }

            Entry& resumed = found->second;

            // One session is one conversation history, so two prompts from one turn would append to it in an
            // order neither prompt can predict: the second waits for the next turn.
            if (resumed.hasRun && resumed.lastParentSessionId == parentSessionId
                && resumed.lastTurnEpoch == turnEpoch)
            {
                return std::unexpected(SubagentError{
                    .errorCode = SubagentSessionBusyErrorCode,
                    .message   = std::format("subagent session '{}' was already resumed in this turn: one session "
                                             "takes one prompt per turn, so send the second prompt next turn",
                                             *resumeSessionId)});
            }

            // The lifetime cap exists because resuming is unbounded otherwise: once it is spent, only a brand
            // new subagent with a fresh allowance can do more work.
            if (resumed.turnsAllowed >= SubagentLifetimeMaxTurns)
            {
                return std::unexpected(SubagentError{
                    .errorCode = SubagentTurnCapErrorCode,
                    .message   = std::format("subagent session '{}' was already granted all {} turns one session "
                                             "may spend over its whole lifetime (SubagentLifetimeMaxTurns): start a "
                                             "new subagent instead",
                                             *resumeSessionId,
                                             SubagentLifetimeMaxTurns)});
            }

            // A resume can only top up what is left of the lifetime allowance, so the granted count never
            // passes the cap however many resumes arrive.
            unsigned const extraTurns = std::min(turns, SubagentLifetimeMaxTurns - resumed.turnsAllowed);
            resumed.turnsAllowed += extraTurns;
            resumed.session->GrantTurns(extraTurns);
            entry = &resumed;
        }

        // Remember the turn that ran it, which is what the same-turn refusal above compares against.
        entry->hasRun              = true;
        entry->lastParentSessionId = std::string(parentSessionId);
        entry->lastTurnEpoch       = turnEpoch;

        SubagentTurnOutcome const outcome = entry->session->Run(prompt);

        // The used-turn count is monotonic across resumes even if a session under-reports, so an allowance
        // never appears to grow back.
        entry->turnsUsed = std::max(entry->turnsUsed, outcome.turnsUsed);

        return SubagentRunResult{ .sessionId          = entry->session->Id(),
                                  .ok                 = outcome.ok,
                                  .errorCode          = outcome.errorCode,
                                  .content            = outcome.content,
                                  .turnsUsed          = entry->turnsUsed,
                                  .turnsAllowed       = entry->turnsAllowed,
                                  .stoppedAtTurnLimit = outcome.stoppedAtTurnLimit };
    }

    // Forgets one live session, destroying it and every byte of its history. Returns false when no such
    // session was live, which includes an id that was already released.
    bool Release(std::string_view sessionId) { return m_live.erase(std::string(sessionId)) > 0; }

    // Releases every live session: what the orchestrator does when the main session ends, because nothing
    // else in the process owns them.
    void ReleaseAll() { m_live.clear(); }

    [[nodiscard]] std::size_t LiveCount() const { return m_live.size(); }

private:
    struct Entry
    {
        std::unique_ptr<LiveSubagentSession> session;
        unsigned                             turnsAllowed = 0;
        unsigned                             turnsUsed = 0;
        // Which turn of which parent last ran this session, to refuse two resumes from one turn.
        std::string                          lastParentSessionId;
        unsigned long long                   lastTurnEpoch = 0;
        bool                                 hasRun = false;
    };

    std::unordered_map<std::string, Entry> m_live;
};
