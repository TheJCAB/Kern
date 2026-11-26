#include "SubagentSessions.h"

#include "TestUtilities.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

bool Contains(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string_view::npos;
}

// Bookkeeping the fake sessions report back through, so a test can see what the registry did without reaching
// into it.
struct World
{
    std::size_t              factoryCalls = 0;
    std::vector<std::string> createdIds;
    // The turn budget each new session was built with, in creation order.
    std::vector<unsigned>    creationBudgets;
    // Turns handed to live sessions by GrantTurns, summed over every resume.
    unsigned                 grantedAfterCreation = 0;
    // When true a session reports fewer turns than it has spent, to prove the registry's counter never shrinks.
    bool                     reportLowerTurnsUsed = false;
};

// A subagent session with no model behind it: it appends every prompt to its own history, which is what proves a
// resume continued the same conversation instead of starting a new one.
class FakeSession final : public LiveSubagentSession
{
public:
    FakeSession(std::string id, unsigned const maxTurns, World& world)
        : m_id{ std::move(id) }
        , m_world{ world }
        , m_budget{ maxTurns }
    {
    }

    [[nodiscard]] std::string const& Id() const override { return m_id; }

    SubagentTurnOutcome Run(std::string_view prompt) override
    {
        m_history.push_back(std::string(prompt));
        ++m_turnsUsed;

        std::string content;
        for (std::string const& entry : m_history)
        {
            content += entry;
            content += ';';
        }

        // A lying session is how the monotonic guarantee is tested: the registry must keep the higher count.
        unsigned const reported = m_world.reportLowerTurnsUsed ? std::min(m_turnsUsed, 1u) : m_turnsUsed;

        return SubagentTurnOutcome{
            .ok                 = true,
            .errorCode          = {},
            .content            = std::move(content),
            .turnsUsed          = reported,
            .stoppedAtTurnLimit = false,
        };
    }

    void GrantTurns(unsigned const extraTurns) override { m_world.grantedAfterCreation += extraTurns; }

    [[nodiscard]] unsigned Budget() const { return m_budget; }

private:
    std::string              m_id;
    World&                   m_world;
    unsigned                 m_budget = 0;
    unsigned                 m_turnsUsed = 0;
    std::vector<std::string> m_history;
};

SubagentSessions::Factory MakeFactory(World& world)
{
    return [&world](unsigned const maxTurns) -> std::unique_ptr<LiveSubagentSession>
    {
        ++world.factoryCalls;
        std::string id = std::format("fake-{}", world.factoryCalls);
        world.createdIds.push_back(id);
        world.creationBudgets.push_back(maxTurns);
        return std::make_unique<FakeSession>(std::move(id), maxTurns, world);
    };
}

} // namespace

int main()
{
    std::println("starting a subagent returns its session id and budget");
        World           world;
        SubagentSessions sessions;
        auto const      factory = MakeFactory(world);

        auto const started = sessions.Run(factory, "parent", 5u, std::nullopt, "first", 1u);
        Expect(started.has_value(), "starting a subagent must succeed");
        ExpectEqual(started->sessionId, std::string{ "fake-1" }, "a new subagent must report the id its session assigned");
        ExpectEqual(started->turnsAllowed, 5u, "a new subagent must be granted the turns the call asked for");
        ExpectEqual(started->turnsUsed, 1u, "a run must report the turns it spent");
        ExpectEqual(sessions.LiveCount(), std::size_t{ 1 }, "a started subagent must stay live so it can be resumed");
        ExpectEqual(world.factoryCalls, std::size_t{ 1 }, "one session must be built per start");

    std::println("resuming a subagent continues its history and keeps its used-turn count monotonic");
        auto const resumed = sessions.Run(factory, "parent", 3u, std::string_view{ started->sessionId }, "second", 2u);
        Expect(resumed.has_value(), "resuming a live subagent must succeed");
        ExpectEqual(resumed->sessionId, started->sessionId, "a resume must report the same session id");
        Expect(world.factoryCalls == 1, "a resume must not build a second session");
        Expect(Contains(resumed->content, "first") && Contains(resumed->content, "second"),
            "a resume must see the prompt the session already answered");
        ExpectEqual(resumed->turnsUsed, 2u, "the used-turn count must keep counting across resumes");

        world.reportLowerTurnsUsed = true;
        auto const lied            = sessions.Run(factory, "parent", 2u, std::string_view{ started->sessionId }, "third", 3u);
        Expect(lied.has_value(), "resuming a live subagent must succeed");
        ExpectEqual(lied->turnsUsed, 2u, "a session reporting fewer turns must not shrink the used-turn count");
        world.reportLowerTurnsUsed = false;

    std::println("a resume's turns are added to the session's budget");
        ExpectEqual(resumed->turnsAllowed, 8u, "a resume's turns must be added to the budget the session already has");
        ExpectEqual(lied->turnsAllowed, 10u, "every resume must add its turns to the budget");
        ExpectEqual(world.grantedAfterCreation, 5u, "every resumed turn must be granted to the live session itself");
        ExpectEqual(sessions.LiveCount(), std::size_t{ 1 }, "resuming must not add a session");

    std::println("a resume of a session that is not live is an error, not a new session");
        auto const unknown = sessions.Run(factory, "parent", 4u, std::string_view{ "no-such-session" }, "prompt", 9u);
        Expect(!unknown.has_value(), "resuming an unknown session must fail");
        ExpectEqual(unknown.error().errorCode, std::string_view{ UnknownSubagentSessionErrorCode },
            "an unknown session must report its own error code");
        Expect(!unknown.error().message.empty(), "an unknown session error must say what went wrong");
        ExpectEqual(sessions.LiveCount(), std::size_t{ 1 }, "an unknown session must not create one");
        ExpectEqual(world.factoryCalls, std::size_t{ 1 }, "an unknown session must not build a session");

    std::println("two resumes of one session in the same turn are refused");
        World            busyWorld;
        SubagentSessions busySessions;
        auto const       busyFactory = MakeFactory(busyWorld);

        auto const firstTurn = busySessions.Run(busyFactory, "parent", 4u, std::nullopt, "one", 7u);
        Expect(firstTurn.has_value(), "starting a subagent must succeed");

        auto const sameTurn = busySessions.Run(busyFactory, "parent", 4u, std::string_view{ firstTurn->sessionId }, "two", 7u);
        Expect(!sameTurn.has_value(), "a second resume of one session in the same turn must be refused");
        ExpectEqual(sameTurn.error().errorCode, std::string_view{ SubagentSessionBusyErrorCode },
            "a same-turn duplicate must report its own error code");

        auto const nextTurn = busySessions.Run(busyFactory, "parent", 4u, std::string_view{ firstTurn->sessionId }, "two", 8u);
        Expect(nextTurn.has_value(), "the same session must be resumable on the next turn");

        auto const otherParent = busySessions.Run(busyFactory, "other-parent", 4u, std::string_view{ firstTurn->sessionId }, "three", 8u);
        Expect(otherParent.has_value(), "a session resumed from a different parent turn must not look like a duplicate");

    std::println("the live-session cap is refused until one is released");
        World            capWorld;
        SubagentSessions capSessions;
        auto const       capFactory = MakeFactory(capWorld);

        for (std::size_t index = 0; index < MaxLiveSubagentSessions; ++index)
        {
            auto const live = capSessions.Run(capFactory, "parent", 2u, std::nullopt, "prompt", index);
            Expect(live.has_value(), "every subagent up to the cap must start");
        }

        auto const overCap = capSessions.Run(capFactory, "parent", 2u, std::nullopt, "prompt", MaxLiveSubagentSessions);
        Expect(!overCap.has_value(), "a subagent beyond the cap must be refused");
        ExpectEqual(overCap.error().errorCode, std::string_view{ SubagentSessionLimitErrorCode },
            "the cap must report its own error code");
        ExpectEqual(capSessions.LiveCount(), MaxLiveSubagentSessions, "the refused call must not add a session");

        Expect(capSessions.Release(capWorld.createdIds.front()), "releasing a live session must succeed");
        auto const afterRelease = capSessions.Run(capFactory, "parent", 2u, std::nullopt, "prompt", 99);
        Expect(afterRelease.has_value(), "a released slot must free room for a new subagent");

    std::println("the lifetime turn cap refuses to grant more turns");
        World            capTurnsWorld;
        SubagentSessions capTurnsSessions;
        auto const       capTurnsFactory = MakeFactory(capTurnsWorld);

        auto const longLived = capTurnsSessions.Run(capTurnsFactory, "parent", SubagentLifetimeMaxTurns - 5u, std::nullopt, "prompt", 1u);
        Expect(longLived.has_value(), "a subagent may be born with most of the lifetime allowance");

        auto const topUp = capTurnsSessions.Run(capTurnsFactory, "parent", 20u, std::string_view{ longLived->sessionId }, "prompt", 2u);
        Expect(topUp.has_value(), "a resume may top up what is left of the lifetime allowance");
        ExpectEqual(topUp->turnsAllowed, SubagentLifetimeMaxTurns, "a resume must never grant more than the lifetime cap");

        auto const beyond = capTurnsSessions.Run(capTurnsFactory, "parent", 5u, std::string_view{ longLived->sessionId }, "prompt", 3u);
        Expect(!beyond.has_value(), "a session that spent its lifetime allowance must not be resumed");
        ExpectEqual(beyond.error().errorCode, std::string_view{ SubagentTurnCapErrorCode },
            "the lifetime cap must report its own error code");

        auto const oversized = capTurnsSessions.Run(capTurnsFactory, "other", SubagentLifetimeMaxTurns + 10u, std::nullopt, "prompt", 1u);
        Expect(oversized.has_value(), "a call asking for more than the lifetime allowance must still start");
        ExpectEqual(oversized->turnsAllowed, SubagentLifetimeMaxTurns,
            "a new session must be clipped to the lifetime allowance");

    std::println("a released session can never be resumed");
        World            releaseWorld;
        SubagentSessions releaseSessions;
        auto const       releaseFactory = MakeFactory(releaseWorld);

        auto const doomed = releaseSessions.Run(releaseFactory, "parent", 4u, std::nullopt, "prompt", 1u);
        Expect(doomed.has_value(), "starting a subagent must succeed");

        std::string const releasedId = doomed->sessionId;
        Expect(releaseSessions.Release(releasedId), "releasing a live session must succeed");
        ExpectEqual(releaseSessions.LiveCount(), std::size_t{ 0 }, "a released session must not stay live");
        Expect(!releaseSessions.Release(releasedId), "releasing an already released session must report that nothing went away");

        auto const gone = releaseSessions.Run(releaseFactory, "parent", 4u, std::string_view{ releasedId }, "prompt", 2u);
        Expect(!gone.has_value(), "a released session must not be resumable");
        ExpectEqual(gone.error().errorCode, std::string_view{ UnknownSubagentSessionErrorCode },
            "a released session must report the unknown-session error code");

        for (unsigned long long epoch = 3; epoch < 5; ++epoch)
        {
            auto const live = releaseSessions.Run(releaseFactory, "parent", 2u, std::nullopt, "prompt", epoch);
            Expect(live.has_value(), "starting a subagent must succeed");
        }
        releaseSessions.ReleaseAll();
        ExpectEqual(releaseSessions.LiveCount(), std::size_t{ 0 }, "releasing everything must leave no live session");

    std::println("a subagent call needs at least one turn");
        auto const noTurns = releaseSessions.Run(releaseFactory, "parent", 0u, std::nullopt, "prompt", 9u);
        Expect(!noTurns.has_value(), "a call with no turn budget must be refused");
        ExpectEqual(noTurns.error().errorCode, std::string_view{ "invalid_argument" }, "a zero budget is a bad argument");

    return 0;
}
