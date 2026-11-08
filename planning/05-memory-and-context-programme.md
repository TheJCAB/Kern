# Memory and context programme

Status: **proposal, nothing implemented.** This document coordinates the four designs numbered 01 to 04, and no source file in this repository has been changed to match them.

Related designs: [01 Context compaction](01-context-compaction.md) — supersession rules, stub format, trigger; [02 Memory tool](02-memory-tool.md) — digest injection, budget and eviction; [03 Session reset](03-session-reset.md) — where a reset may execute, what survives; [04 Shared note store](04-shared-note-store.md) — index in the prompt, tree sharing, naming discipline. Each of the four links back to this programme.

## The problem

`Session::Pimpl::m_conversationHistory` is an append-only log of assistant tool calls and their results; `Prompt` only ever appends. A long run therefore accumulates several versions of the same file (read, edit, read again), repeated `glob`/`grep` queries, and `build` output that later builds supersede — all of it re-sent every turn. Meanwhile the model's reasoning stream is deliberately discarded: only final `content` is stored, so a derivation dies with the message that produced it, even when its conclusion was worth keeping. Each half of that problem gets two proposals.

## The four proposals

| Document | What it does | Cost / cache impact | When it runs |
| --- | --- | --- | --- |
| [01 Context compaction](01-context-compaction.md) — supersession rules, stub format, trigger | Automatic and system-authored: elides redundant tool results into short self-describing stubs by rewriting `content`; never deletes or reorders messages | One prefix-invalidating prefill per pass, repaid only if the reclaim clears `minReclaim`; hysteresis high→low water mark | Top of a turn, immediately before `BuildPayload`, never mid-turn |
| [02 Memory tool](02-memory-tool.md) — digest injection, budget and eviction, subagent inheritance | Model-authored `memory` tool (`add`/`replace`/`remove`/`list`) over a small curated store of decisions, invariants and tasks | Always resident as a ~2k-char digest in a transient trailing message, so the cached prefix still hits | Digest rebuilt once per turn inside the payload; store mutated only on a tool call |
| [03 Session reset](03-session-reset.md) — where a reset may execute, what survives, prompt quality | Wholesale amnesia: `reset_session` replaces everything after the system message with a model-written new prompt | Total prefix rebuild; no turn refund and resets are capped, so it stays rare | Deferred: applied only after the turn's whole tool-result batch is appended |
| [04 Shared note store](04-shared-note-store.md) — index in the prompt, tree sharing, naming discipline | Out-of-context key-value store: `notes` tool (`write`/`read`/`list`/`erase`) for bodies up to 64 KB, with only a one-line-per-entry index resident | ~1500-char index every turn; each body costs one explicit call instead of permanent residency | Index rebuilt per turn; bodies fetched on demand |

## How they fit together

- Must never be forgotten → **memory**: unconditional, resident, small, survives compaction and reset.
- Large and occasionally needed → **notes**: out of context, index resident, body fetched on demand.
- Redundant and re-derivable → **compaction**: mechanical, needs no model intent.
- Whole context no longer useful → **reset**: the distilled conclusions move to memory and notes first.

They share two mechanisms rather than duplicating plumbing: the *transient trailing message* passed to `Session::Pimpl::BuildPayload`, the precedent already used by the "hurry up" path, for anything that must be visible without being stored; and a `std::shared_ptr` held in the by-value-copied `ToolsRuntimeContext`, which is how one store is shared with child subagent sessions for free. Reset alone keeps its pending flag per session in `Session::Pimpl`, because a copyable flag would let a child clear its parent's history.

## Common implementation notes

- Every proposed tool follows the existing header-only convention: a `constexpr ToolDefinition` whose `.callTool` is a reference to an `inline` lvalue handler, with `constexpr ToolParameter` arrays beside it.
- New source and header files are added explicitly to `CMakeLists.txt` — the core block and the `src/Tools/*.h` block, no globbing — and new tests are registered explicitly in `tests/CMakeLists.txt` with their own source list and `add_test`.
- New definitions are registered in `MainTools[]` in `src/main.cpp` and in the child arrays in `src/Tools/research.h` and `src/Tools/implement.h`.
- System prompt text goes into `data/CommonSystemPrompt.txt` so every agent, including children, shares it.
- Observability reuses the existing `logs/` per-turn artifact convention (beside the `request.json`/`response.json` of the turn): `logs/<8char-id>_<NNN>_compaction.json`, `_reset.json`, and `logs/memory-<sessionId>.json` for the store.
- Nothing may write under `build/`, and all file access goes through `context.fs` (`ValidatedFileSystem`), never raw streams.

## Suggested order of work

1. Measure first: metadata only (`id`, `turn`, resource keys) and redundancy logged to `logs/...compaction.json`, with no elision ever.
2. Compaction Tier 1: deterministic stubs and supersession rules, testable without a live service.
3. Memory tool: `add`/`list` plus the transient digest, proving the habit and the cache-stability claim.
4. Note store: write/read/list over the shared store, then the caps and `erase`.
5. Session reset last: it depends on memory and notes already being in place, so that a reset has somewhere durable to leave state.
