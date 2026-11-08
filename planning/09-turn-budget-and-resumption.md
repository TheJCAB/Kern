# Turn budget and session resumption

Status: proposal, nothing implemented. The budget is a fixed constant today, and a run that reaches
it is discarded whole; this plan replaces the constant with a budget that renews on recorded
evidence and adds the ability to continue a session from its transcript. It depends on
[06 Session recording](06-session-recording.md), because a progress predicate and a resume both read
the record that plan defines.

Related designs: [06 Session recording](06-session-recording.md) (whose manifests and turn logs carry
the tool calls, results, usage and finish reasons this plan reasons over), and
[07 Evaluation harness](07-evaluation-harness.md) and
[08 Evaluation programme](08-evaluation-programme.md) (which own the per-case budget field and the
rule that a session case must answer a question the prompt cases cannot). Each of the three links
back to this plan.

## The problem

There are two hard-coded budgets and no way to change either without editing a header. The main
agent's is `constexpr unsigned DefaultMaxTurns = 50;` (`src/CommandLine.h:30`), carried as
`CommandLineOptions::maxTurns` (`src/CommandLine.h:40`) and consumed at `src/main.cpp:296`. A
subagent's is a second, independent constant, `constexpr unsigned SubagentMaxTurns = 50;`
(`src/ToolUtilities.h:46`), passed positionally by the two subagent tools at
`src/Tools/research.h:55` and `src/Tools/implement.h:62`. The two numbers are unrelated; nothing
keeps them equal, nothing derives one from the other, and neither is recorded in a way that lets a
reader reconstruct which value produced a given run.

The deeper problem is what the number is used for. The budget is the loop bound itself, `for
(unsigned turn = 0; turn < maxTurns; ++turn, ++m_log.sequence)` in `Session::Pimpl::RunPrompt`
(`src/Session.cpp:544`), so it is an all-or-nothing gate rather than a cost control. When the loop
runs out, the exhaustion return at `src/Session.cpp:907` produces `.ok = false, .errorCode =
"too_many_turns", .content = "The model took too many turns", .turnsUsed = maxTurns`, which the
subagent tools surface to the parent as `status: "incomplete"` plus `turns_used`. The parent learns
that its child stopped, but not whether the child had finished its work and was about to answer.

Two consequences follow, and both are costly in different currencies. The first is waste: a run that
has done all of its work but has not yet written its answer is thrown away at exactly the same cost
as a run that made no progress at all, so fifty turns of tool calls, edits and builds can be spent
and then discarded by one missing paragraph. The second is irreversibility: `m_conversationHistory`
(`src/Session.cpp:943`) lives only in memory and is never persisted, so the only recovery from
exhaustion is to re-run the whole task from the beginning at full cost, hoping that the second run
happens to be luckier with its sampling. A limit that cannot tell "finished but silent" from "stuck"
has to be set for the worst case, which is why it is simultaneously too small for real work and too
large for a loop.

## What counts as progress

A budget that renews needs a predicate, and the predicate has to be observable from the record
rather than asserted by the model. Progress is one of three things. The first is a tool result that
is new in this session: a result that is not byte-identical to an earlier result for the same tool
and the same arguments, which means the agent looked somewhere it had not looked or asked a question
it had not asked. The second is a state change, which is the stronger class: a file written, a
command run whose exit status differs from the last run of that command, or a build that passes
after one that failed. The third is a change in error class, where a different failure is evidence of
motion even though the turn bought nothing.

Three things are explicitly not progress. The model's own claim that it is nearing completion is not,
because that is the exact sentence a stuck agent repeats; the budget must never renew on the strength
of a self-description. Token counts are not, because a turn can spend thousands of tokens restating
the situation it already knew. The turn number is not, because counting turns is what the budget
already does and it is the thing being questioned.

None of this needs new instrumentation. The recorder already emits the material the predicate
consumes: per-turn `toolCalls[]` and `toolResults[]` with `tool`, `error`, `result`, `resultLength`
and `resultTruncated`, plus `usage`, `responseMs` and `finishReason`, written by the `TurnRecord`
guard described in [06 Session recording](06-session-recording.md) and declared in
`class SessionRecorder` (`src/SessionRecorder.h:48`). The predicate is therefore a function over
lines already written to the turn log, which is what makes it testable without a live endpoint and
reviewable without trusting the agent loop's internals.

## A budget that renews on evidence

The fixed bound becomes a budget structure carrying three values: an initial count, the extension
size granted per checkpoint, and an absolute ceiling that is never exceeded. The structure lives in
`Session::Config` (`src/Session.h:30`), beside `maxTokens` and `systemPrompt`, rather than being a
positional argument to `Prompt`, because a budget is a property of the session and not of one call
into it; a positional argument is how the two subagent call sites ended up with a second constant
that drifted from the first.

The rule is one sentence. One extension is granted per checkpoint at which the progress predicate
fires, the loop continues, and no extension is ever granted on a turn where the predicate does not
fire. A checkpoint is evaluated once per turn, after the tool results for that turn are available,
so the predicate always has the current turn's evidence in hand.

Two finish reasons are needed beside `too_many_turns`, and the distinction between them is the whole
point. `no_progress` means the stall rule below tripped: the run was stopped because it had stopped
moving. `too_many_turns` is reserved for the other case, where extensions were exhausted while
progress was still being made, so a report can tell a run that was cut off mid-work from a run that
was going nowhere. Collapsing the two would restore exactly the ambiguity the change exists to
remove, because "hit the ceiling while working well" and "hit the wall doing nothing" would again be
indistinguishable to whoever reads the transcript.

Extensions have to be visible in the record, or a long run becomes unexplainable. The turn line gains
a boolean progress flag, so each renewal has a cause attached to the turn that justified it, and the
manifest's `outcome` gains the extension count, so a reader can see how many renewals a run used
without replaying the predicate over the log. A long run then has an account of its own length: it
was long because it kept doing new things, and the number of renewals says how many times that
happened.

## The stall rule, which is what makes relaxing the limit safe

Relaxing the limit is only safe if the case that inflates it is bounded from the other side. A budget
that only grows is simply a bigger constant with extra steps. The stall rule is that bound, and it
stops early — with `no_progress`, not `too_many_turns` — after a small number of consecutive turns in
which the progress predicate does not fire, and after any exact repetition of a tool call whose
preceding result it already holds. The second clause is the sharp one: repeating a call whose answer
is already in the history is not a failure of knowledge but a loop, and it is detectable exactly
rather than statistically.

The ceiling is absolute and never renewed. Extensions cannot be chained beyond it, because the
ceiling is not a checkpoint the predicate can re-open; it is the number at which the structure stops
granting extensions no matter what the record says. And the parent's own budget bounds the total
spend of its children: a child session's turns are paid for by the parent's turn, so a subagent tree
can never consume more than the run that started it. The nesting is therefore conservative by
construction rather than by convention, and adding a level of subagents does not multiply the worst
case.

## Resuming an interrupted session

A relaxed budget does not make exhaustion impossible, and the record makes it recoverable, which is
where resumption earns its place. The mechanism has three parts.

The first is the entry point. A new `CommandLineAction` beside the existing `showSessions` one
(`src/CommandLine.h:22`, dispatched at `src/main.cpp:198`) backs a `--resume <session-id>` action,
so resumption is a verb the binary has rather than a script wrapped around it.

The second is the mechanism itself, and it is small. `Session::Config` gains a member carrying a
conversation history, and that history seeds `m_conversationHistory` (`src/Session.cpp:943`, seeded
today at `src/Session.cpp:417`) instead of the session starting from only the system message. That
is all "resume" means mechanically: the loop begins with the messages the earlier run ended with, and
the budget starts fresh because a resumed run is a new run.

The third is the subagent path. The `research` and `implement` tools gain an optional `sessionId`
parameter, so a parent can continue a child it has just watched run out of turns, and that path is
bounded by a small explicit cap on the number of resumes of the same session. Without the cap the
cheap way to ignore every budget would be to resume in a loop.

A resumed run is a new session in the transcript directory, not a rewrite of the old one. Its
manifest's `identity` names the session it continued, and its `provenance` records the resume count,
so evidence is never silently spliced and two records never have to be merged to know what happened.
The history is rebuilt from the inputs group of the original manifest — the system prompt and the
tool definitions — plus the turn log, which is why [06 Session recording](06-session-recording.md)
stores those two texts verbatim rather than as digests.

That reconstruction is lossy on one axis and exact on the other, and the difference must be stated.
Tool results are stored truncated, with `resultTruncated` set, so a resumed run sees an excerpt where
the original saw the whole result; a resumed run is therefore approximate for its history of tool
output and exact for the system prompt and the tool definitions it starts from. The harness must be
able to tell a resumed record from a fresh one, precisely because one of the two may have failed for
a reason the other could not have.

## What this must not become

The constraints are hard and few. No path may exist on which the loop advances without the progress
predicate being evaluated, because a bypass converts the budget back into a counter with a story
attached. A budget never renews on the strength of anything the model says about itself, so an agent
that states it is nearly done gains nothing from saying so. A resumed session cannot be resumed into
an unbounded chain, so the cap is part of the mechanism rather than a guard bolted onto it. And an
evaluation case always pins its own budget, because a paired comparison between two runs that spent
different amounts of budget measures the budget rather than the change.

The last of those is the one that protects everything else.
[07 Evaluation harness](07-evaluation-harness.md)'s per-case `budget` field — maximum turns, maximum
tokens and a wall-clock ceiling — pins the spend so a delta is attributable to the change under test,
and a renewing budget does not exempt a case from pinning it: the case states its ceiling, and the
run may renew up to it. [08 Evaluation programme](08-evaluation-programme.md)'s requirement that a
session case answer a question the prompt cases cannot is what bounds the cost of the whole
argument, because a session-level case is the only place a budget policy can be observed at all and
the only place where observing it is expensive.

## Common implementation notes

The two constants become defaults rather than the only source of the value (`src/CommandLine.h:30`,
`src/ToolUtilities.h:46`): they name what a session gets when nobody says otherwise, and the value
actually used arrives through the budget structure in `Session::Config`.

The change reaches the loop in exactly two places: the bound at `src/Session.cpp:544` and the
exhaustion return at `src/Session.cpp:907`, which becomes two returns, one per finish reason. The
predicate and the resume-from-manifest reader are new definitions, so they go in the
`add_executable(kern ...)` source list in `CMakeLists.txt:31-59`; the build does not glob, and a new
file that nobody listed does not link.

Hermetic tests belong in `tests/CMakeLists.txt` with their own source list and `add_test`, exercising
the progress predicate and the resume-from-manifest reader against a fixed fixture rather than a live
endpoint. Both are deterministic functions over text, so both are exactly the kind of thing the CTest
suite is for: a fixture turn log in, a verdict out, no network.

Nothing may be persisted to enable resume except through the recorder, so that a resumed run leaves
exactly the record a fresh run leaves. A second persistence path would mean a transcript that can be
resumed but not read, or read but not resumed, and the two would drift apart the first time either
changed.

## Risks and open questions

A progress predicate can be gamed by an agent that makes cheap novel calls, reading a different file
on every turn without ever changing anything; the mitigation under consideration is to require the
state-change class, not novelty alone, before an extension is granted, which makes the predicate
stricter at the cost of refusing renewals to honest exploration.

Resumption duplicates a run's cost rather than removing it. The tokens already spent are spent, so
resuming is a way to keep the work that was already done, not a way to avoid work; a resumed run pays
for its own turns on top of the ones that produced the history it seeds.

The lossy tool results mean a resumed run may fail for reasons the original would not have, because
it is reasoning from an excerpt where the original saw the whole result. The leaning is to record the
resume count in `provenance` so a report can separate those failures from genuine ones rather than
absorbing them into a pass rate.

It is genuinely open whether the parent or the child should own the resume decision. A parent knows
what it still needs and can weigh a resume against doing the work itself; a child knows what it was
in the middle of, but only before it is torn down. The current leaning is the parent, because the
parent is the one holding the budget.

And it is open whether the ceiling should be expressed in tokens rather than turns, given that
`usage` is now recorded and a turn is a coarse unit that can cost ten times another. Turns are
legible and cheap to enforce; tokens are the real currency and are now measurable. The likely answer
is both, with turns as the outer bound and tokens as the renewal unit, but that is not decided here.
