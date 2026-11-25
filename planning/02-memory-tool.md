> **Superseded.** The design below (`src/Memory.h`, `src/Memory.cpp`, `src/Tools/memory.h`, the per-turn digest and the store shared by the whole session tree) has been removed. It is replaced by the stateless in-place `note` tool in `src/Tools/note.h`: a note lands in the turn that made it and is replayed in order like any other message, nothing is collected or re-appended, `obsolete` only records that a thought no longer applies, and dropping notes is left to compaction. Notes are per session and are never shared with subagents.

# Memory tool design

Status: in progress.

Related designs: [01 Context compaction](01-context-compaction.md) (mechanical elision of superseded tool results, which must pin the digest), [03 Session reset](03-session-reset.md) (deliberate amnesia, the thing memory must survive), [04 Shared note store](04-shared-note-store.md) (out-of-context bodies, the complement to a resident digest), and [05 Memory and context programme](05-memory-and-context-programme.md) (the programme this design is part of).

## Problem

Reasoning is captured but discarded: only the final `content` reaches the conversation history, and that history is never persisted, so a derivation dies with the message that produced it. A decision or an invariant the model reached ("the digest must stay prefix-cache stable", "`ToolDefinition::callTool` is a reference, so a handler must be an lvalue") is gone once the motivating tool result leaves the window or is elided, and the model re-derives it or contradicts it. Subagents make it worse: `research` and `implement` start a fresh `Session` with their own system prompt and tool list, so nothing the parent learned crosses the boundary unless something is put there deliberately.

The store, the `memory` tool, the resident digest and the sharing of the store down the session tree all exist. What is still missing is what makes the habit worth the budget it spends: a way to make room when the store is full, a way for what was remembered to outlive the process that remembered it, and a policy for what a child is allowed to write into its parent's store.

## How the store works today

| part | where | what it is |
| --- | --- | --- |
| entries | `src/Memory.h:17-23` | `id`, `kind` (`fact`, `decision`, `invariant`, `task`), `text`, `createdBy`. Nothing else: there is no creation turn and no pin. |
| store | `src/Memory.h:28-71` | one ordered `std::map<std::string, MemoryEntry>` (`src/Memory.h:66`) with `Add`, `Replace`, `Remove`, `List`, `Digest`, `Empty`. |
| caps | `src/Memory.h:31-36` | 40 entries, 500 characters each, 8000 in total, digest 2000 by default and accepted between 200 and 20000. A call that would exceed a cap is refused rather than stored. |
| tool | `src/Tools/memory.h:139-154` | one `memory` tool, `operation` = `add`, `replace`, `remove`, `list`; `id`, `text`, `kind`, `maxChars` optional. Registered for the orchestrator (`src/main.cpp:50-56`) and for both child sessions (`src/Tools/research.h:25-31`, `src/Tools/implement.h:27-36`). |
| digest | `src/Memory.cpp:248-269` | one line per entry, `- [kind] id: text`, in id order, inside a fixed header, with a `[+N more; call memory(list)]` footer only when entries were clipped. |
| sharing | `src/main.cpp:161-164`, `src/main.cpp:193` | one `std::shared_ptr<MemoryStore>` created beside each `ToolsRuntimeContext`. The context is copied by value into every child (`src/main.cpp:175`), so the pointer copies with it and one store serves the whole tree. A session handed a context without a store makes one for itself (`src/Session.cpp:86-92`). |
| digest placement | `src/Session.cpp:153-159` | the digest is the last transient message of the request and is never pushed into the history, so the cached prefix is untouched while the store does not change, and a change costs only the tail. Transient messages are appended after the history (`src/Conversation.cpp:44-47`). An empty store adds no message at all. |
| prompt text | `data/CommonSystemPrompt.txt:15` | the paragraph that teaches every agent, including children, what belongs in memory and what does not. |

Three invariants, stated because everything below has to keep holding:

- `Digest` is a pure function of the store: entries come out in id order with no timestamps, so the same store gives byte-identical text on every turn and the provider's prefix cache keeps hitting on everything before the digest. A creation timestamp must therefore never enter the digest; it may only enter `list`.
- There is no locking. `CallTool` runs synchronously inside the turn loop, so one session never mutates the store while reading it. Concurrent tool calls, or one store shared by concurrent sessions, would need a `std::mutex` with the capacity check and the insert in one critical section.
- The whole store is a process-lifetime object and nothing writes it out. A new process starts with an empty store, and a session that starts afresh has nothing to remember.

## Subagent write-back policy

A child session gets its parent's store for free and its writes are visible to the parent immediately, because the store is shared through a `std::shared_ptr` (`src/main.cpp:161-164`). What a child is allowed to keep in the parent's 8000-character budget is undecided, and so is what `createdBy` should be: it is filled with the parent session id today (`src/Tools/memory.h:46`) and reaches the model through `list` but never through the digest.

| option | mechanism | verdict |
| --- | --- | --- |
| propagate everything | nothing to do: this is what the code does today | Right for `decision` and `invariant", which are exactly what a parent must not contradict, but a chatty `implement` can fill the parent's budget with entries the parent never asked for and cannot see being added. |
| propagate by kind | `fact` and `task` stay local to the writing agent, `decision` and `invariant` reach the whole tree | Needs a scope on the entry and a filter in `Digest` and `List`, and no new argument: propagation becomes a property of the content rather than of the caller. |
| propagate on request | a new `propagate: true` argument, default off for `implement` | Cheapest to implement, but it leaves the most valuable content - the invariants a subagent discovered - out of the parent's store unless the child remembers to ask. |

Whichever is chosen, a subagent's incidental findings should not silently compete with the orchestrator's own content, and the parent must be able to `remove` anything a child added by id. The choice should be made against a recorded transcript (plan 07) rather than by taste, because the question is how much context a subagent is worth.

## Eviction

The caps in `src/Memory.h:31-36` are enforced by refusal: `add` answers `full` and the model has to `remove` or shorten something by hand. That is deliberate while there is nothing to order by. Pending:

- Restore `createdAtTurn` to `MemoryEntry`. It was dropped from the first implementation because a timestamp in the digest would break cache stability every turn; it may be stored and reported by `list`, but `Digest` must never render it.
- Add a `pinned` flag, and a `prune` sweep that runs before injection and never from `add`: remove the oldest unpinned `fact` by `createdAtTurn`, then the oldest unpinned `task`, and never an `invariant`, a pinned entry, or a child's entry that the parent has not yet seen.
- When pinned entries alone exceed the digest budget, do not truncate silently: emit the pinned lines, then a footer naming how many characters are pinned and asking the model to remove entries, and keep refusing further `add` calls.
- Keep `list` answering full texts for every entry including the ones the digest left out, so eviction costs visibility in the prompt but never data. This already holds today (`src/Memory.cpp:208-232`) and must keep holding.

## Persistence

Nothing is written out, so a session reset (plan 03) or a lost process costs every entry the model wrote; that is the largest remaining piece of this design.

- One JSON file per session, `logs/memory-<sessionId>.json`, written through `context.fs` (`ValidatedFileSystem::WriteTextFile`, `src/FileValidation.h:45`) so that path validation applies and `Kern.build.json`, `.git` and `build/` stay unreachable.
- Its contents are the entries, a schema version and the session id, with no timestamp beyond `createdAtTurn`, so that the file is diffable and byte-stable between runs.
- Loading is best effort: a missing file, a parse error or a version mismatch yields an empty store and a one-line notice, never a failed session.
- `logs/` beside the executable is already the convention for per-turn artifacts, so a sibling file costs nothing. A `state/` directory is the alternative if `logs/` turns out to be wiped between runs, at the cost of creating it.

## Interaction with compaction

Plan 01 proposes eliding superseded tool results into short stubs. The two features must not fight:

- A digest message is transient and never part of the history, so the compaction pass can never reach it, and its characters never count against the compaction budget.
- When compaction stubs a tool result, the memory entry derived from it survives: the stub is the evidence, the entry is the conclusion. That is the whole point of writing it down.
- The compaction pass must skip digest messages when computing supersession keys, otherwise a digest line that happens to quote a file path would read as a competing observation of that file and be elided as superseded.
- A memory entry never retroactively elides the tool result it came from; the two run independently.

## Risks and open questions

- Stale entries. The digest is injected unconditionally, so an entry the model stopped believing is as visible as a true one. The prompt paragraph addresses the habit (`data/CommonSystemPrompt.txt:15`) and `createdBy` plus the restored `createdAtTurn` give `list` something to judge an entry by; neither is a guarantee.
- Character accounting. The caps are counted in characters, while the context window is quoted in tokens; whether the digest budget and the 8000-character store cap should share a unit with the window measurement is still open.
- Sharing a mutable store across a session tree with no locking is safe only while tool calls stay synchronous. Parallel subagents, or one store used by concurrent sessions, would need the `std::mutex` described above.
- Whether `createdBy` belongs in the digest at all, or only in `list`, is open, and with it whether a child's additions should be counted to the parent as a number.
- A long-lived digest of 2000 characters every turn is real money in the window; whether the default budget should shrink when the store is nearly full is open.

## Phased rollout

Each phase is independently useful:

1. Eviction: restore `createdAtTurn` and add `pinned`, then the prune sweep and the over-budget footer. Proves the store can be kept inside its budget without the model hand-deleting entries.
2. Persistence: the per-session JSON file and its best-effort loading, so that a session reset or a restart keeps what was decided.
3. Subagent write-back policy: one of the three options above, once the orchestrator's own habit is settled and a corpus exists to judge it against.
4. The compaction guard of the section above, which only matters once plan 01 lands.
