# Session reset design

Status: proposal, nothing implemented. Anything not verified against existing code is marked *proposed*.

Related designs: [01 Context compaction](01-context-compaction.md) (the cheap, frequent, additive complement that elides rather than discards), [02 Memory tool](02-memory-tool.md) (the always-resident digest a reset may carry across with `keep_memory`), [04 Shared note store](04-shared-note-store.md) (out-of-context bodies and a resident index that survive a reset wholesale), and [05 Memory and context programme](05-memory-and-context-programme.md) (coordinates the four designs).

## Problem
`Prompt` (`src/Session.cpp:912`) only appends: the assistant `tool_calls` message, then one
`role="tool"` message per call (`src/Session.cpp:812-825`). A long run carries its whole traversal — dead
ends, superseded file versions, exploratory reads, builds later redone — yet the useful payload of a
200-message history is usually a dozen facts. Compaction (design 01) shrinks that mass but preserves
*narrative continuity*: it keeps the shape of the traversal and only elides the bulk. Once a task changes
phase (survey done, plan fixed, remaining work is a list of edits) continuity itself is the waste; what
is wanted is a deliberate amnesia that keeps the distilled conclusions only.

## Design summary
A `reset_session` tool whose single substantive argument is a **new prompt**: a self-contained statement
of what still matters (state of the world, decisions, remaining plan). On success the session discards
`m_conversationHistory` beyond the system message and replaces it with that prompt. The model writes its
own replacement context — no second summarizer call, no heuristic selection.

## The hard constraint: where a reset may execute
A tool call runs *inside* the turn loop, between pushing the assistant message carrying `tool_calls` and
pushing the `role="tool"` results. The OpenAI dialect requires every `assistant.tool_calls[].id` to be
answered by a matching `role="tool"` message, and the loop pushes those results
(`src/Session.cpp:812-825`) **after** the handler returns (via `CallTool`, `src/ToolUtilities.cpp:233-251`).
A handler that cleared history itself would leave unanswered `tool_calls` (reset before the batch) or
orphan `role="tool"` messages (reset after) — request-shape errors, not merely bad content. So the reset
is **deferred**: the handler sets a flag, honoured only once the whole tool-result batch is appended.

```cpp
// proposed: src/Tools/reset_session.h — the handler never touches history
inline json ResetSessionTool(json const& arguments, ToolsRuntimeContext const& context)
{
    std::string const newPrompt = arguments.value("new_prompt", "");  // length window -> too_short/too_long
    return json{
        { "ok", true }, { "deferred", true },
        { "note", "history will be replaced after this turn's tool results" },
        { "__kern_deferred_reset", newPrompt },   // reserved key, stripped by the loop (see Registration)
    };
}
// proposed: src/Session.cpp, immediately after the tool-result loop at :826
if (m_pendingReset) {                     // std::optional<std::string>, Session::Pimpl member
    ApplyPendingReset(*m_pendingReset);    // keep history[0], push one user message; clears hurry state
    m_pendingReset.reset();
}
```

`ApplyPendingReset` replaces `m_conversationHistory` with `{system message, user message(newPrompt)}` and
logs it. The next request is a valid transcript: the assistant `tool_calls` message and its results
disappear together, in one step.

**Rejected alternative:** apply the reset immediately and synthesize a fake tool result to keep the
pairing legal. Worse: that synthetic result is either discarded in the same breath (dead code with a
visible side effect) or kept and sent, and the model is then re-primed by text it never wrote and cannot
inspect; the handler also claims a result for a call it is deleting. Deferral is unambiguous: *this turn
finishes normally, then the transcript is replaced*.

## Argument surface

| Name | Type | Required | Default | Meaning |
| --- | --- | --- | --- | --- |
| `new_prompt` | string | yes | — | Complete replacement context; the only `user` message after the reset |
| `reason` | string | no | `""` | Free text for log and console; never sent to the model |
| `keep_memory` | bool | no | `true` | Keep the design-02 memory store across the reset |
| `keep_notes` | bool | no | `true` | Keep the design-04 note store across the reset |

```json
{ "type": "object", "required": ["new_prompt"], "additionalProperties": false,
  "properties": {
    "new_prompt":  { "type": "string", "minLength": 80, "maxLength": 8000 },
    "reason":      { "type": "string" },
    "keep_memory": { "type": "boolean", "default": true },
    "keep_notes":  { "type": "boolean", "default": true } } }
```

Proposed bounds: under 80 chars after trimming -> `errorCode "too_short"` (a two-word reset destroys
context without replacing it); over 8000 -> `"too_long"` (the replacement must not blow the budget it
exists to protect); missing or non-string `new_prompt` -> `"invalid_argument"` (per `ToolArgumentError`,
`src/ToolUtilities.h:79-82`). A rejected call sets no pending flag.

## What survives a reset

| Artifact | Fate |
| --- | --- |
| `m_conversationHistory` beyond `[0]` | dropped: assistant messages, tool calls, tool results, nudges |
| System message `[0]` | kept verbatim, incl. the max_tokens paragraph (`src/Session.cpp:409-417`) |
| Memory store (design 02) | kept iff `keep_memory` (default true) |
| Note store (design [04 Shared note store](04-shared-note-store.md)) | kept iff `keep_notes` (default true) |
| Turn counter | **kept** — reset refunds nothing (next section) |
| Logs | kept, append-only; writes `logs/<id>_<NNN>_reset.json` |
| File system state | untouched — the reset is about context, not the workspace |
| `hurryMessages`/`hurryRetries` (`:541-543`) | cleared: they describe the old transcript |

## Why the turn counter must not be refunded
`for (unsigned turn = 0; turn < maxTurns; ++turn, ++m_log.sequence)` (`src/Session.cpp:544`) with the
exhaustion return `"too_many_turns"` (`:907`) is the only termination guarantee in the loop. A refund
would let a model reset every turn forever: the loop never reaches `maxTurns` and the session bills until
cancelled. The loop continues with the same `turn`; `turnsUsed` counts all turns. Additionally cap resets
per session (proposed `MaxResets = 3`) and return `errorCode "too_many_resets"`.
## Prompt quality

| Must contain | Must not contain |
| --- | --- |
| Current workspace state (what exists, compiles, fails) | Apologies, meta-commentary, "I will now…" |
| Decisions and invariants already fixed | Narrative of what was tried and abandoned |
| What is *verified* versus what is *assumed* | File contents a `read_file_chunk` re-derives |
| Remaining plan as ordered concrete steps | Tool-call transcripts or ids |
| Pointers: exact paths, symbols, line ranges | Speculation about the code stated as fact |

```txt
Task: add a --no-log flag to kern. Build is green as of now.
Verified: option list ends at src/CommandLine.cpp:205; --model parsing at :141-144 is the pattern to copy.
Assumed: no test asserts the full option set (unchecked).
Decisions: flag named --no-log, no short form, logging defaults on.
Plan: 1) bool noLog in Options 2) parse "--no-log" 3) gate LogFileState in Pimpl 4) README 5) build.
Open: whether the log id is printed unconditionally (src/Session.cpp:419).
Reopen: src/CommandLine.h, src/CommandLine.cpp:60-205, src/Session.cpp:396-419.
```
Good: a state delta a fresh reader can act on — every line verified, decided, or planned.
```txt
I have been exploring the codebase for a while and looked at many files.
I tried several approaches but they did not work out.
Let me continue with the CLI flag now. Parsing is in main.cpp somewhere, I think.
Sorry for the slow progress.
```
Bad: a status report that presumes the reader saw the work — exactly what a reset destroys.

## Interaction with subagents
Children created through `CreateNewSessionFunction` (`src/ToolUtilities.h:54`) start with no history, so
no reset is needed to isolate them; the feature pays off in long-running orchestrators, and inside 50-turn
`SubagentMaxTurns` (`src/ToolUtilities.h:46`) sessions that burn turns exploring before a mechanical edit
phase. The pending flag must live **per session**, in `Session::Pimpl` (`src/Session.cpp:54`) — never
in `ToolsRuntimeContext`, copied by value into every child (`src/ToolUtilities.h:56-67`, built at
`src/main.cpp:153-175`); a flag there would let a child clear its parent's history.

## Interaction with compaction
Complementary ([01 Context compaction](01-context-compaction.md)); compaction is cheap, frequent and lossless-ish, reset
is expensive (a full prefix rebuild plus one turn of context lost at the swap) and lossy but total.

| Situation | Prefer |
| --- | --- |
| Long mechanical loop, near-duplicate tool results, same phase | compaction |
| Phase change: survey -> plan -> implement, or a fully abandoned approach | reset |
| History still needed by the remaining plan (specific snippets, ids) | compaction |
| Remaining work re-derives from files on disk | reset |
| Budget pressure with few turns left | compaction (cheaper: keeps prefix semantics) |
| Repeated compaction reclaiming little while the transcript grows | reset |

## Interaction with the prompt cache
A reset invalidates the entire prefix: the next request shares nothing with the previous one. Acceptable —
it happens at most a few times per session, and a total prefix rebuild is exactly what the feature is for.
To stay cache-friendly afterwards the reset prompt must be the **single** `user` message directly after
the system message, so later turns append to a stable prefix. Do not split it into several messages, and
do not re-inject it as `assistant` text.

## Registration and implementation sites

| Site | Edit |
| --- | --- |
| `src/Tools/reset_session.h` | new header: `Handler`, `constexpr ToolParameter Params[]`, `constexpr ToolDefinition Definition` |
| `CMakeLists.txt:48-56` | add `src/Tools/reset_session.h` to the header block |
| `src/main.cpp:44-49` | add `reset_session` to `constexpr ToolDefinition MainTools[]` |
| `src/Tools/research.h:21` / `implement.h:25` | optional: a subagent may need it; weigh `SubagentMaxTurns` against one more tool's tokens in every child prompt |
| `Session::Pimpl` (`src/Session.cpp:54`) | `std::optional<std::string> m_pendingReset;`, `unsigned m_resetCount = 0;` |
| `Session::Pimpl` | `void ApplyPendingReset(std::string newPrompt);` |
| `src/Session.cpp:826` | after the tool-result loop, apply `m_pendingReset` |
| `CallTool` result handling | strip `"__kern_deferred_reset"` before `BuildToolResponseMessage` |

The indirection is forced by the ABI: `ToolCallable` returns only `json` and takes `ToolsRuntimeContext
const&` (`src/ToolUtilities.h:76`), so a handler has no writable channel to the session. Proposed
convention: the reserved key `"__kern_deferred_reset"` in the returned object; the loop reads it,
re-validates it, sets `m_pendingReset`, and strips the key so the model never sees it.
## Observability
- console: `reset [{}]> reason: {} | messages {} -> {} | ~{} tokens dropped | reset {}/{}`
- `logs/<id>_<NNN>_reset.json` (proposed): `reason`, `oldMessageCount`, `newMessageCount`,
  `estimatedTokensDropped`, `resetIndex`, `turnIndex`; the token figure is a length heuristic — there is
  no tokenizer (design 01 makes the same point)
- new `--no-reset` flag (proposed), next to design 01's `--no-compaction`; when set the tool is not
  registered, so the model is never tempted
## Testing
`tests/SessionResetTest.cpp` in `tests/CMakeLists.txt`, using the existing explicit-source pattern
(`target_include_directories(... ../src)`, `add_test`), ~6 cases:
1. a deferred reset keeps the `tool_call`/`tool`-result pairing valid in the next payload;
2. reset drops history and keeps the system message byte-identical, max_tokens paragraph included;
3. the turn counter is not refunded and `too_many_turns` is still reachable;
4. `too_short` (and `too_long`) rejected with history untouched;
5. the reset cap returns `too_many_resets` on the fourth attempt;
6. a child's reset leaves the parent's `m_conversationHistory` intact.
## Risks and open questions
- **Resetting to dodge a failing task.** A stuck model can erase the evidence; the cap and `_reset.json`
  make it visible, not impossible. `reason` is self-reported.
- **Lost constraint.** A rule stated once inside a tool result is gone if the model omits it; no safety
  net beyond `keep_notes`. **Reset thrash** — reset then re-reading everything dropped — is net negative.
- **Hurry messages.** The transients at `src/Session.cpp:831-883` describe the old output and must be
  cleared on reset, else the next request mixes a fresh prompt with a stale nudge.
- **Edge cases.** Two resets in one batch: last wins, one counted (proposed); a compaction and a reset
  both pending for a turn: the reset wins and compaction is skipped.

## Phased rollout
- **Phase 1** — tool, deferred flag + `__kern_deferred_reset` plumbing, `ApplyPendingReset`, logging,
  `--no-reset`.
- **Phase 2** — reset cap (`too_many_resets`), `keep_memory` / `keep_notes`, finalized reset record.
- **Phase 3** — automatic reset *suggestion* (never execution) when the compaction pass reclaims less
  than X over N consecutive turns.
