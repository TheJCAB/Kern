# Memory tool design

Status: proposal, nothing implemented.

Related designs: [01 Context compaction](01-context-compaction.md) (mechanical elision of superseded tool results, which must pin the digest), [03 Session reset](03-session-reset.md) (deliberate amnesia, the thing memory must survive), [04 Shared note store](04-shared-note-store.md) (out-of-context bodies, the complement to a resident digest), and [05 Memory and context programme](05-memory-and-context-programme.md) (the programme this design is part of).

## Problem

Reasoning is captured but discarded: only `content` reaches `Session::Pimpl::m_conversationHistory`
(`src/Session.cpp:943`), and that history is never persisted across sessions. So a decision or invariant the
model derived — "the digest must stay prefix-cache stable", "`ToolDefinition::callTool` is a reference, so
handlers must be lvalues" — lives only in the message that produced it. Once the motivating tool result
scrolls out of the window, or the history is compacted, the derivation is gone: the model re-derives it, or
contradicts it. Subagents make this worse, because `research`/`implement` start a fresh `Session` that does
not inherit the parent's history, so nothing the parent learned crosses the boundary.

There is no self-authored writable store in the tool set today. Every tool either reads the workspace or
writes files the user owns; none holds "what I decided" on the model's own behalf.

## Design summary

One new tool, `memory`, with four operations — `add`, `replace`, `remove`, `list`. Each entry is a short
titled fact, decision, invariant or task with a stable id (`d-digest-injection`, `i-no-locking`). Entries
live in a store shared by the whole session tree and are injected into every prompt as a compact digest
(§6), so the model curates its durable context in ~2k characters instead of carrying it in the transcript.

Two features, deliberately not overlapping:

| Memory (this document) | Out-of-context note store ([04 Shared note store](04-shared-note-store.md)) |
| --- | --- |
| Always in context as a digest, every turn, every session in the tree | Out of context; addressed by key, read on demand |
| Small, curated, hard char budget; the model deletes to make room | Large, unbounded bulk payloads; the point is not to spend context |
| Content: durable decisions, invariants, open tasks | Content: arbitrary blobs — file dumps, notes, intermediate artifacts |
| Written once, read many times without a tool call | Every read is an explicit tool call |
| Purpose: stop re-deriving what was already settled | Purpose: move bytes out of the window |

Rule of thumb: if the model would be wrong without it, memory; if it merely might want it again, note store.

## Data model

Proposed: new `src/Memory.{h,cpp}` declaring `class MemoryStore`, held as `std::shared_ptr<MemoryStore>` in
`ToolsRuntimeContext` (`src/ToolUtilities.h:56-67`), constructed beside the other members in `main.cpp`
(`src/main.cpp:153-175`). A `shared_ptr` because the context is copied **by value** into child sessions —
`CreateNewSessionFunction` takes `ToolsRuntimeContext const&` and `Session::Config.toolContext` copies it —
so a value member would give every subagent a private empty store, and a reference would dangle. The whole
session tree must share one store; the shared pointer makes that a construction-time guarantee.

```cpp
struct MemoryEntry
{
    std::string id;
    enum class Kind { fact, decision, invariant, task } kind;
    std::string text;
    unsigned    createdAtTurn;
    std::string createdBy;   // "root" or the subagent tool name that added it
    bool        pinned;
};

class MemoryStore
{
public:
    json Add(std::string_view id, MemoryEntry::Kind kind, std::string const& text,
             unsigned turn, std::string const& createdBy);   // errors: full / too_long / invalid_argument
    json Replace(std::string_view id, std::string const* text,
                 std::optional<MemoryEntry::Kind> kind);     // error: not_found
    json Remove(std::string_view id);                        // error: not_found
    json List(std::string_view prefix = {}) const;           // full entries, not the digest
    std::string Digest(std::size_t maxChars) const;          // §6, sorted by id

private:
    std::map<std::string, MemoryEntry> m_entries;  // ordered => deterministic, id-sorted digest
    std::size_t m_maxEntries{ 64 };
};
```

Concurrency: single-threaded per session today — `CallTool` (`src/ToolUtilities.cpp:233-251`) runs
synchronously inside `Session::Pimpl::Prompt` (`src/Session.cpp:912`) — so no locking is proposed. If
tool calls ever parallelize, or one store is shared by concurrent sessions, mutations must take a
`std::mutex`, `Digest()` must copy under it, and `Add`'s capacity check plus insert must be one critical
section so `m_maxEntries` cannot be exceeded.

## Tool surface

| argument | required | type | notes |
| --- | --- | --- | --- |
| `operation` | yes | string | `add` \| `replace` \| `remove` \| `list` |
| `id` | no | string | required for `add`/`replace`/`remove`; optional prefix filter for `list` |
| `text` | no | string | required for `add`; required for `replace` unless `kind` is given |
| `kind` | no | string | `fact` (default) \| `decision` \| `invariant` \| `task` |
| `maxChars` | no | integer | digest budget for `list`, default 2000 |

Skeleton in the existing header-only style; the handler is an lvalue because
`ToolDefinition::callTool` is a reference (`src/ToolUtilities.h:89-96`):

```cpp
// src/Tools/memory.h
namespace Tools::memory
{
inline json Handler(json const& arguments, ToolsRuntimeContext const& context)
{
    std::string const operation = arguments.value("operation", "");
    if (!context.memory) { return json{ {"error", "no memory store"}, {"errorCode", "unavailable"} }; }
    if (operation == "add")     { return context.memory->Add(...); }
    if (operation == "replace") { return context.memory->Replace(...); }
    if (operation == "remove")  { return context.memory->Remove(...); }
    if (operation == "list")    { return context.memory->List(arguments.value("id", "")); }
    throw ToolArgumentError{ std::format("unknown operation '{}'", operation) };
}

constexpr ToolParameter RequiredParameters[] = {
    StringToolParameter{ { "operation", "One of add, replace, remove, list." } },
};
constexpr ToolParameter OptionalParameters[] = {
    StringToolParameter { { "id",   "Stable short id, e.g. 'd-digest-injection'." } },
    StringToolParameter { { "text", "The fact, decision, invariant or task, in one or two sentences." } },
    StringToolParameter { { "kind", "fact (default), decision, invariant or task." } },
    IntegerToolParameter{ { "maxChars", "Digest character budget for list; default 2000." },
                          .minimum = 200, .maximum = 20000 },
};
constexpr ToolDefinition Definition{
    .name               = "memory",
    .description        = "Keep a small, durable store of decisions, invariants, facts and open tasks. "
                          "Entries are shown to you automatically in every prompt; add only what you would "
                          "otherwise re-derive.",
    .requiredParameters = RequiredParameters,
    .optionalParameters = OptionalParameters,
    .callTool           = Handler,
};
}
```

Examples — `invalid_argument` comes from throwing `ToolArgumentError`, the rest are returned as
`{"error":...,"errorCode":...}` objects, matching the existing error convention:

```json
{"operation":"add","id":"d-digest-injection","kind":"decision","text":"Digest is injected as a transient trailing message, never written to history."}
{"ok":true,"id":"d-digest-injection","entries":3,"chars_used":512}
{"operation":"replace","id":"i-no-locking","text":"MemoryStore is unguarded; Prompt runs tool calls synchronously."}
{"ok":true,"id":"i-no-locking","entries":3}
{"operation":"add","kind":"todo","text":"..."}
{"error":"unknown kind 'todo'; expected fact, decision, invariant or task","errorCode":"invalid_argument"}
{"operation":"remove","id":"d-missing"}
{"error":"no memory entry 'd-missing'","errorCode":"not_found"}
{"operation":"add","id":"f-huge","text":"<6000 chars>"}
{"error":"entry exceeds 500 characters","errorCode":"too_long"}
{"operation":"add","id":"f-61","text":"..."}
{"error":"store is full (64 entries); remove one first","errorCode":"full"}
```

## Digest injection

| option | shape | verdict |
| --- | --- | --- |
| (a) extra `role="system"` message after the main system prompt | static history position, must be kept in sync with the store | Rejected: two system messages invite provider-specific merging, and any store change invalidates the cache from that point on. |
| **Recommended** — the precedent already exists for the "hurry up" path (`src/Session.cpp:831-883`). |
| (c) rewriting the system prompt | text changes with the store | Rejected: the system prompt is the cache prefix; editing it on every `add` invalidates the entire cached prefix. |

Option (b) keeps the prefix stable: history and system prompt are untouched, and the only variable text is
the final message. While the store does not change, that trailing message is byte-identical from turn to
turn, so the provider's prefix cache still hits on everything before it; when the store does change, only
the tail is re-sent. Implementation is one appended `ConversationMessage{ .role = "user", .content = ...
digest ... }` passed to `BuildPayload` and never pushed into `m_conversationHistory`.

Digest format — one line per entry, sorted by id, inside a short fixed wrapper:

```
[Memory — entries you asked to keep. Update with the memory tool.]
- [decision] d-digest-injection: Digest is injected as a transient trailing message.
- [invariant] i-no-locking: MemoryStore has no locking; tool calls are synchronous.
- [task] t-finish-doc: Write sections 7+ of the memory review.
[+3 more; call memory(list)]
```

Exactly one line per entry as `- [kind] id: text`, ordered by id (not by insertion time), so the digest is a
pure function of the store. Lines are emitted until the character budget is exhausted — default 2000 chars,
overridable per `list` call via `maxChars` — with `pinned` entries first so they always survive the budget.
The `+N more; call memory(list)` footer appears only when entries were dropped, and `list` returns full
texts, so nothing is unreachable.

Refresh timing: the digest is recomputed once per turn, when the next request is built. A mutating tool call
already returns its own confirmation in the tool result, so no mid-turn notification is needed: the tool
result stays the only in-turn feedback and the change is not duplicated in context.

## Inheritance across subagents

`research` and `implement` each create a **fresh** `Session` with their own system prompt from `data/` and a
restricted tool list; the child never sees the parent's `m_conversationHistory` and its only input is the
`prompt` argument (`src/Tools/research.h:28`, `src/Tools/implement.h:35`). So nothing the parent knows crosses
the boundary unless it is put there explicitly. Three proposed ways to do that:

| option | mechanism | verdict |
| --- | --- | --- |
| (i) digest prefixed into the child's system prompt | `createNewSession` concatenates parent digest + `data/ResearchSystemPrompt.txt` when building `Session::Config` | Workable, but it makes the child's cache prefix depend on the parent's store, and it conflates two different prompts. |
| (ii) digest embedded in the child's first user message | the parent's `prompt` argument is rewritten to `"<digest>\n\n<task>"` before the session starts | Simple, no engine change, but the child cannot tell instructions from memory, and the parent must remember to do it. |
| (iii) hand the child the parent's store directly | the same `std::shared_ptr<MemoryStore>` already in `ToolsRuntimeContext` | **Recommended** — the context is *copied* into the child, so the `shared_ptr` copies with it and the store is shared for free; nothing to thread, nothing to escape. |

Option (iii) is not just simplest, it is already implied by §3's data model: `toolContext` is copied by value
into the child (`src/ToolUtilities.h:54`, `src/main.cpp:153-175`) and the store is a `shared_ptr`, so a child
created through the existing `CreateNewSessionFunction` reads and writes the *same* entries with zero new
plumbing, and its digest appears through the normal §6 path. That makes (i) and (ii) unnecessary; keep the
digest out of `prompt` so the parent's text stays the only task input.

Proposed write-back policy, since (iii) means a child's writes are visible to its parent immediately:

- Entries added by a child default to `createdBy = "<tool name>"` (`research`, `implement`) and to
  session-tree scope, i.e. they are visible to the whole tree.
- `kind = invariant` and `kind = decision` propagate up: they are exactly the "settled" content a parent must
  not contradict.
- `kind = fact` stays local to the writing agent unless a proposed new argument `propagate: true` is passed;
  a child's incidental discoveries should not consume the orchestrator's budget.
- `kind = task` never propagates: a child's todo list is not the parent's.

Risk: a child that spams `add` pollutes the orchestrator's context with entries it never asked for and cannot
see being added, which then compete with the parent's own content under the §8 cap. Mitigations, all
proposed: `createdBy` surfaces in the parent's digest as a count, the parent may `remove` anything by id, and
`implement` (the verbose one) is the natural candidate for `propagate: false` by default if it gets noisy.

## Budget and eviction

Memory competes with tool results for the same context window, so it must have a hard ceiling rather than
"the model will be reasonable". Proposed caps:

| cap | value | rationale |
| --- | --- | --- |
| total characters across all entries | 8000 | ~2k tokens; large enough for a real design log, small enough that it cannot dominate a turn |
| entries | 40 | bounds the digest line count independently of text length |

`add` fails with `errorCode: "full"` when either cap would be exceeded, and names the cap that was hit; the
model is expected to `remove` or shorten something. Eviction — used only by a proposed `prune` sweep before
injection, never by `add` — removes in this order:

1. the oldest unpinned `fact`, by `createdAtTurn`;
2. then the oldest unpinned `task`;
3. then nothing: `invariant`, `pinned`, and entries with `createdBy != "root"` that are pending write-back
   are never evicted silently.

If pinned entries alone exceed 8000 characters, injection does not truncate silently either: the digest emits
the pinned lines and then `[memory over budget: N chars pinned; remove entries]`, and the next `add` returns
`full`. `memory(list)` always returns **full** text for every entry, including entries evicted from the
digest; the digest footer `+N more` (§6) is the pointer, so eviction costs visibility in the prompt but never
data.

## Persistence

Proposed: a JSON file per session, `logs/memory-<sessionId>.json`, written through `context.fs`
(`ValidatedFileSystem::WriteTextFile`, `src/FileValidation.h:45`) so path validation applies and `Kern.build.json`,
`.git` and `build/` stay unreachable. `logs/` beside the executable is already the convention for per-turn
artifacts — `WriteLogFile` (`src/Session.cpp:80`), `BuildLogPath` (`:326`), `GetLogsDirectory` (`:316`) — so a
sibling file costs nothing and never lands under `build/`. Proposed alternative: a `state/` directory if `logs/`
is wiped between runs, at the cost of creating it.

Contents are the entries, a schema version and the session id; no timestamps beyond `createdAtTurn`, so the
file is diffable and byte-stable. Loading is **best-effort**: a missing file, a parse error or a version
mismatch yields an empty store and a one-line notice, never a session failure. The point is that a session
reset ([03 Session reset](03-session-reset.md)) or a restart mid-task does not lose what the model decided.

## Interaction with compaction

[01 Context compaction](01-context-compaction.md) proposes eliding redundant tool results and replacing them with stubs. The
two features are complementary and must not fight:

- Digest messages are transient and `pinned`; the compaction pass never elides them, and never counts their
  characters against its own budget.
- When compaction stubs a tool result, the memory entry derived from it **survives** — that is the entire
  point of writing it down. Anything worth keeping beyond the stub belongs in memory; the stub is only the
  evidence, the entry is the conclusion.
- The compaction pass must skip digest messages when computing supersession keys, otherwise a digest line
  that happens to quote a file path would be treated as a competing observation of that file and elided as
  "superseded".
- A memory entry does not retroactively elide the tool result it came from; the two run independently.

## System prompt changes

Proposed: append this paragraph to `data/CommonSystemPrompt.txt`, which every agent loads:

```txt
You have a `memory` tool for durable notes. Use it for what you would otherwise have to re-derive: decisions
you made and why, invariants the code must keep, constraints the user stated, and approaches that failed so
you do not repeat them. Keep each entry to one or two sentences with a stable id, and remove entries that
turn out to be wrong. Do not store file contents, line numbers or other details that a `read_file_chunk` can
look up again, and do not record the mechanics of the current turn — what you just ran, what you are about to
run, tool errors you already fixed. If an entry stops being true, replace or remove it; a stale entry is worse
than no entry.
```

The last sentence matters most: the digest is injected unconditionally, so an entry the model forgot to update
is indistinguishable from a fact.

## Registration

Proposed edits, all mechanical:

- `CMakeLists.txt:48-56` (the `src/Tools/*.h` header block): add `src/Tools/memory.h`.
- `src/main.cpp:44-49`: add `Memory::Definition` to `constexpr ToolDefinition MainTools[]`, and
  `#include "Tools/memory.h"` with the other tool includes.
- `src/Tools/research.h:21` and `src/Tools/implement.h:25`: add `Memory::Definition` to each child tool array
  (and the include) so subagents can read and write the shared store (§7).
- `CMakeLists.txt:32-44` (the core block): add `src/Memory.cpp` and `src/Memory.h`.
- Every `tests/CMakeLists.txt` binary whose source list contains `src/Session.cpp` also gets `../src/Memory.cpp`.
Includes to add, verbatim: `#include "Memory.h"` and `#include "Tools/memory.h"` in `src/main.cpp`;
`#include "Tools/memory.h"` in `src/Tools/research.h` and `src/Tools/implement.h`.

## Testing

Proposed `tests/MemoryTest.cpp`, seven cases:

| case | expectation |
| --- | --- |
| `add`/`list`/`replace`/`remove` round-trip | entry appears with full text, edits apply, `remove` empties the store and `list` returns `entries: 0` |
| duplicate `add` with an existing id | rejected as `invalid_argument`, existing entry unchanged |
| `replace` on a missing id | `not_found`, store unchanged |
| eviction under the char cap | adding past 8000 chars reports `full`; after eviction the oldest unpinned `fact` is gone and the `invariant` remains |
| digest formatting | byte-identical output for the same store across calls, lines sorted by id, footer only when truncated |
| pinned entries | survive eviction and appear before unpinned lines even when the budget is exceeded |
| bad arguments | missing `id` on `add`, unknown `operation`, unknown `kind` → `invalid_argument` |

The digest test is the important one: it must assert on exact bytes, because §6's cache-stability claim only
holds if the formatting is deterministic. `tests/CMakeLists.txt` gets a new binary with an explicit source
list, `target_include_directories(... ../src)` and `add_test`, matching the neighbours.

## Alternatives considered

| alternative | rejection reason |
| --- | --- |
| a free-form scratchpad the model appends to | unbounded, unstructured, and the model duplicates its own discarded reasoning in it; no ids means no targeted `replace`/`remove` and no deduplication |
| keeping memory only in the system prompt | any edit rewrites the cached prefix (§6c), so every `add` costs a full re-prefill |
| relying on compaction digests alone | compaction has no model intent — it keeps what is frequent, not what was decided — and it dies with the session, so nothing survives a reset |
| a plain file the model edits with `write_file` | pollutes the user's workspace, races with real edits, needs a tool call to read back, and has no budget or validation |

## Risks, open questions, and phased rollout

- Stale entries: the model writes a decision, then changes the decision and forgets to update the entry. The
  digest is unconditional, so the wrong entry is as visible as the right one. Mitigation: the prompt text in
  §11 and `createdAtTurn` in `list` output.
- Digest noise for small tasks: two injected lines per turn where nothing is remembered. Mitigation: emit no
  wrapper at all when the store is empty.
- Character accounting: chars vs. tokens, and whether `maxChars` (§6) and the 8000-char store cap share a unit.
- Sharing a mutable store across a session tree with no locking — safe only while tool calls stay synchronous
  (§3); parallel subagents would need the `std::mutex` described there. Whether `createdBy` belongs in the
  child's digest at all, or only in `list`, is open.

Phased rollout, each phase independently useful:

1. `add`/`list` only, no persistence, digest injected as the transient trailing message of §6. Proves the
   habit and the cache-stability claim before the store can grow.
2. `replace`/`remove`, the §8 caps and eviction, and §9 persistence — the store becomes durable across
   session resets.
3. Subagent sharing (§7) and `invariant`/`decision` propagation with `propagate: true`; only worth doing once
   the parent's own workflow is settled.
