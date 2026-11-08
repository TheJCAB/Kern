# Shared note store design

Status: proposal, nothing implemented.

Related designs: [01 Context compaction](01-context-compaction.md) (which the store lets stub tool results aggressively, because a deposited body survives out of context), [02 Memory tool](02-memory-tool.md) (the resident digest notes must not replace, since a note can be missed), [03 Session reset](03-session-reset.md) (which drops the history but keeps the store and its index), and [05 Memory and context programme](05-memory-and-context-programme.md) (coordinates the four designs).

## Problem

A session's context is the only place this agent can accumulate knowledge, and it is the worst possible
place for most of it. It is expensive: every byte of a tool result is re-sent on every subsequent turn.
It is lossy: compaction (design 01) replaces old tool results with derived stubs, so anything the model
noticed but did not act on can vanish. It is destroyed by reset (design 03), which drops the whole
`m_conversationHistory`. And it is **not shared with subagents**: `research` (`src/Tools/research.h:28`)
and `implement` (`src/Tools/implement.h:35`) build a FRESH `Session` with a restricted tool list and a
system prompt; the child does not inherit the parent's history, and the only thing crossing back is the
`summary` field of its result JSON.

Meanwhile the facts most worth keeping are exactly the ones too detailed to keep resident: the full call
graph of a subsystem (14 hops, one line each, 400 lines of `grep` output), a table of every error message
and its origin, or the full text of a long analysis of which only the conclusion matters today but the
argument will be needed again when the conclusion is questioned.

## Design summary

An out-of-context key-value store, "notes". Each entry has a short descriptive key, a one-line summary,
and a body of arbitrary length. Every agent in the session tree carries only the **index** (key + summary,
one line each) in its context, and pulls a body on demand with `recall`.

What the model sees, always, in every session of the tree (a few lines, not the payloads):

```
- notes.media.transcode-callgraph: which handler calls which, 14 hops
- notes.errors.seen: every error message hit this session, with the call site
- notes.analysis.hurry-up-path: full argument for the transient-message shortcut
```

What `recall` returns for `media.transcode-callgraph`: the complete body — `ProbeMedia -> ParseHeader ->
SelectBackend -> ...` down all 14 hops — in one tool result, once, instead of the model re-deriving it
from `grep` output three times and carrying the intermediate results forever.

## Three stores, three roles

| | Memory ([02 Memory tool](02-memory-tool.md)) | Notes (this design) | Compaction stubs ([01 Context compaction](01-context-compaction.md)) |
| --- | --- | --- | --- |
| In context | always, as a digest every turn | index only (key + summary) | the stub itself, replacing the result |
| Size | small, hard char budget | arbitrary per body, up to 64 KB | as long as the summary renders |
| Content | decisions, invariants, open tasks | bulk artifacts: call graphs, error tables, analyses | redundant tool results |
| Authored by | the model, deliberately | the model, deliberately | the system, derived automatically |
| Read cost | free | one `recall` call per body | free |
| Purpose | stop re-deriving what is settled | move bytes out of the window, keep the pointer | reclaim window space without intent |

Selection rule, one sentence: if it must never be forgotten, it goes to memory; if it is large and needed
only occasionally, it goes to notes; if it is a redundant tool result the system can re-derive, compaction
handles it.

## Data model

Proposed: new `src/Notes.{h,cpp}` declaring `class NoteStore`, held as `std::shared_ptr<NoteStore>` inside
`ToolsRuntimeContext` (`src/ToolUtilities.h:56-67`), constructed beside the other members in `main.cpp`
(`src/main.cpp:126-161`). The `shared_ptr` is the whole sharing mechanism: the context is COPIED BY VALUE
into child sessions (`CreateNewSessionFunction` takes it by const reference, `src/ToolUtilities.h:54`), so
a value member would give every subagent a private empty store and a reference would dangle. Because
`research`/`implement` already pass their `ToolsRuntimeContext` down, notes are shared across the entire
session tree with no change to the subagent tools at all; children can recurse and still hit the same store.

```cpp
struct Note
{
    std::string key;              // normalized, dotted: "media.pipeline.callgraph"
    std::string summary;          // one line, the only part that is resident
    std::string body;             // arbitrary payload, <= 64 KB
    unsigned    createdAtTurn;
    std::string createdBy;        // "root" or the subagent tool name that wrote it
    std::size_t bodyBytes;
};

class NoteStore
{
public:
    json Write(std::string_view key, std::string_view summary, std::string const& body,
               unsigned turn, std::string const& createdBy);   // full replace; returns old bodyBytes
    json Read(std::string_view key) const;                     // body + metadata
    json Erase(std::string_view key);
    json List(std::string_view prefix = {}) const;             // keys + summaries + sizes
    std::string Index() const;                                 // resident block, one line per entry

private:
    std::map<std::string, Note> m_notes;                       // ordered => deterministic index
};
```

Keys are normalized by the store, not by the caller: lowercased, restricted to `[a-z0-9._-]`, at most 48
characters, an optional dot hierarchy (`media.pipeline.callgraph`) so `List`/`Index` can group by prefix.
`Write` on an existing key is a **full replace** (never a merge) and returns the replaced entry's
`bodyBytes` so the model can see what it just overwrote. Concurrency: one session is single-threaded, and
the store is shared by a session tree that runs one agent at a time — no locking today. If subagents ever
run in parallel, add a `std::mutex` inside `NoteStore` and take it in all five members; the `shared_ptr`
and the API do not change.

## Tool surface

| | Single `notes` tool with `operation` | Three tools (`note_write`/`note_read`/`note_erase`) |
| --- | --- | --- |
| Tool definitions | 1 (`ToolDefinition` in every `MainTools[]` and child array) | 3, repeated in `src/main.cpp:42-47`, `research.h:21`, `implement.h:25` |
| Schema tokens | one shared block, same shape as the `memory` tool | three descriptions, three schemas |
| Model choice | one tool to remember, one `operation` value | three names to remember, easier to pick wrong |
| Validation | one handler, one `switch`, one error vocabulary | three handlers, duplicated key/size checks |

Recommendation: **one** `notes` tool with an `operation` switch (`write`, `read`, `list`, `erase`), matching
the memory tool of design 02, so the tool surface stays small in every list that repeats it and the limits
and error codes live in one place.

| Argument | Required | Notes |
| --- | --- | --- |
| `operation` | yes | `write` \| `read` \| `list` \| `erase`; anything else -> `invalid_argument` |
| `key` | for `write`, `read`, `erase` | normalized; empty or > 48 chars -> `invalid_argument` |
| `summary` | for `write` | one line, <= 120 chars; missing -> `invalid_argument` |
| `body` | for `write` | <= 64 KB, may contain newlines; missing -> `invalid_argument` |

Proposed skeleton, in the existing header-only style (`inline Handler` + `constexpr ToolParameter Params[]`
+ `constexpr ToolDefinition def`; note `callTool` is a REFERENCE, so `Handler` must be an lvalue):

```cpp
namespace Tools::notes
{
constexpr ToolParameter Params[] = { StringToolParameter{ { "operation", "write | read | list | erase" } } };
constexpr ToolParameter OptionalParams[] = { StringToolParameter{ { "key", "normalized dotted key" } },
                                             StringToolParameter{ { "summary", "one line, <= 120 chars" } },
                                             StringToolParameter{ { "body", "payload, <= 64 KB" } } };

inline json Handler(json const& arguments, ToolsRuntimeContext const& context)
{
    NoteStore& notes = *context.notes;          // shared_ptr, copied into child contexts
    std::string const op = arguments.value("operation", "");
    // try/catch: ToolArgumentError -> {"error":..., "errorCode":"invalid_argument"}, per ToolUtilities.h:78-82
    // then dispatch to notes.Write / Read / List / Erase and return its json unchanged.
}

constexpr ToolDefinition def{ .name = "notes", .description = "...",
                              .requiredParameters = Params, .optionalParameters = OptionalParams,
                              .callTool = Handler };
}
```

Example calls and results:

```json
{ "operation": "write", "key": "media.transcode-callgraph", "summary": "which handler calls which, 14 hops",
  "body": "ProbeMedia -> ParseHeader -> ..." }
{"ok": true, "key": "media.transcode-callgraph", "bodyBytes": 3120, "replacedBodyBytes": 0, "entries": 3}

{ "operation": "read", "key": "media.transcode-callgraph" }
{"ok": true, "key": "media.transcode-callgraph", "summary": "which handler calls which, 14 hops",
 "body": "ProbeMedia -> ParseHeader -> ...", "createdBy": "research", "createdAtTurn": 6}

{ "operation": "read", "key": "media.pipeline.callgraph" }
{"ok": false, "error": "no note with that key", "errorCode": "unknown_key"}

{ "operation": "write", "key": "deps.manifest", "summary": "full manifest", "body": "<65537 bytes>" }
{"ok": false, "error": "body exceeds 65536 bytes", "errorCode": "too_large"}

{ "operation": "write", "key": "Notes With Spaces", "summary": "", "body": "x" }
{"ok": false, "error": "key must match [a-z0-9._-] and be <= 48 chars", "errorCode": "invalid_argument"}
```

The fifth error code, `index_full`, fires when a `write` would create a 61st entry: the model must erase
something first, exactly like the memory store's budget, so the resident index stays one screen.

Exact limits proposed: body max **64 KB**, summary max **120 chars**, key max **48 chars**, index max
**60 entries**. Both `summary` and `key` are counted after normalization; the index of 60 entries at
~80 chars each is ~5 KB resident, which is affordable in every child session of the tree.
## How the index enters the prompt

Proposed: three ways to place the same `NoteStore::Index()` text in front of the model; the choice is only
about *where* it sits in the payload.

| | (a) Dedicated `role="system"` message | (b) Transient trailing message | (c) Folded into the memory digest (design 02) |
| --- | --- | --- | --- |
| Mechanism | extra system message built by `BuildPayload` | reuse `BuildPayload(transientMessages)` (`src/Session.cpp:101-148`) | prefix of the `memory` block |
| Precedent | none: a new message shape in every provider path | the "hurry up" path (`src/Session.cpp:651-703`) | none yet |
| Stored? | must not be, or N stale copies | no: never appended to `m_conversationHistory` | no |
| Coupling | touches every provider's message assembly | confined to `Session::Pimpl` | entangles two features that should fail independently |
| Weight | system-level, competes with the real system prompt | user-turn-level: "here is current state" | system-level, inside a block already at budget |

Recommend **(b)**: a proven path, no history leakage, no per-provider change. Rebuilt **once per turn** where
the payload is built and never mutated between turns, so ordering is by key and two payloads over an unchanged
store are byte-identical (no timestamps, no sizes, no markers). Exact proposed body:

```txt
Available notes (call notes(operation="read", key=...)):
- build.error-msbuild-2025: MSVC 2025 error text and the file that emits it
- media.callgraph: which handler calls which, 14 hops
+7 more (call notes(operation="list"))
```

One line per entry, `- <key>: <summary>`, sorted; truncation is the footer only, and only when the block would
exceed its budget. Proposed budget **1500 chars** including the header, footer reserved first. Refreshed at turn
boundaries only: a `write` in turn N appears in turn N+1, so no turn sees a half-updated index.

## Sharing across the session tree

The whole point. One `NoteStore` per session tree, reached through the `shared_ptr` in the copied
`ToolsRuntimeContext` (`src/ToolUtilities.h:56-67`); every child built via `CreateNewSessionFunction` gets a
by-value context copy and therefore the same store — and children can recurse and still hit it. No plumbing
change in `research.h`/`implement.h`: they already pass the context down.

Why this beats the current channel, one `summary` string per child:

- A child's analysis is usually longer than the parent needs. Today it compresses into `summary` and the rest
  dies with the child session; with notes the child deposits the argument and the parent reads one key — or
  never.
- The child gains too: it can move its own bulky intermediates out of context mid-run instead of carrying them
  for the rest of its 50-turn budget (`SubagentMaxTurns`, `src/ToolUtilities.h:46`).
- The parent pays index lines, not payloads.

Worked example:

1. The orchestrator writes `plan.phase2` = "move callgraph rendering out of `Session.cpp`, keep the payload
   byte-identical" — ~2 KB of reasoning.
2. It delegates: `implement("Implement plan.phase2; read notes(operation=\"read\", key=\"plan.phase2\") first.")`
   The prompt carries the *key*, not the content.
3. The child reads it, works, writes `impl.phase2.touched-files` (files plus why each changed). On return the
   orchestrator reads that note and iterates, without having spent context on the child's exploration.

The enabling requirement: the delegation prompt only needs key names. Everything shared lives in the store, so
a prompt can stay one line no matter how much context both sides must agree on.

## Naming discipline

The index is one namespace shared by the whole tree and resident everywhere, so a key must be recognisable
without its body and cheap to keep.

Proposed convention: `<area>.<topic>[.<subtopic>]`, lowercase, dots as separators, 2-4 segments, dashes inside
a segment, no hand-written version numbers or dates unless the fact is inherently dated.

| Good | Why | Bad | Why not |
| --- | --- | --- | --- |
| `build.error-msbuild-2025` | area, symptom, the thing that dates it | `errors` | no area, no topic |
| `media.callgraph` | area + artifact, recognisable alone | `callgraph-analysis-final-v2` | noise words, hand-versioned |
| `decision.no-shell-tool` | a decision, findable when questioned | `notes1` | a sequence number as a name |
| `impl.phase2.touched-files` | writer-scoped: who, which phase, what | `tmp` / `scratch` | collides with the next agent's `tmp` |

The model is told the convention in the shared system prompt (§13), so children with their own prompts still
produce the same shape. Duplicate keys overwrite: `write` is a full replace returning `replacedBodyBytes`, so a
collision shows up in the tool result instead of silently.

## Provenance and garbage collection

Each note records `createdBy` (agent path: `"root"`, `"research"`, or a nested path) and `createdAtTurn`.
Proposed: `notes(operation="list")` returns keys with their writer, age and `bodyBytes`, so the model can tell
fresh from stale and know whom to ask.

Proposed GC: on session end the store is discarded unless persisted (§11). Within a session nothing is evicted silently: exceeding the 60-entry cap makes `write` fail with `index_full` and the model must erase, because silent eviction would make a later `read` fail for a reason the model cannot see, and a loud refusal at write time is strictly better. Erase-while-referenced is allowed: bodies are re-derivable, so the failure mode is one wasted `grep`, not a lost fact.

Counterargument, stated honestly: an unread note is out of context and costs one index line, so eviction pressure is about **index size**, not storage. The right response to a full index is "write a better summary or merge two notes", and the `index_full` text should say so.

## Persistence (optional, off by default)

Proposed `--notes-file <path>`: the whole store serialized on session end and loaded on start as one
pretty-printed JSON object (`{"notes":[{"key","summary","body","createdBy","createdAtTurn"}]}`) written through
`context.fs` (`src/FileValidation.h:23`) like every other file access. Refuse paths inside `build/` or on the
protected list; refuse to overwrite an existing file unless `--notes-overwrite`. Off by default, never
automatic: no default path, no implicit resume.

Use case: resume. A run that dies at turn 40 leaves its conclusions on disk, so the next run re-reads only the
bodies it needs instead of paying 40 turns again; and a finished run stays inspectable afterwards.

Risk, hence the default: stale notes are actively dangerous. "The retry loop lives in `Retry.cpp:88`" is
confident, terse and wrong after the next task edits that file, and nothing on the note says so. A dead run's
notes are trustworthy only for the same task — so explicit flag, explicit path, explicit overwrite.

## Interaction with compaction and reset

- The index message is **pinned**: compaction ([01 Context compaction](01-context-compaction.md)) may elide tool results and
  old reasoning, never the system prompt or the index — it is small and it maps everything else.
- A tool result whose content was deposited as a note is **safe to stub aggressively**: the stub survives, the
  body survives out of context, `read` restores it. This is the strongest argument for the feature — notes let
  compaction stop being conservative and replace a 40 KB `grep` result with one line.
- Reset ([03 Session reset](03-session-reset.md)) keeps the store: the history dies, the index is rebuilt from the store
  in the first turn of the fresh session. So the reset prompt need not restate bodies, only keys — which are
  already in the index — plus "continue phase 2, the notes are still there".
- Memory ([02 Memory tool](02-memory-tool.md)) is the opposite: resident, must never be missed, survives reset as a
  digest. Notes survive as pointers; a conclusion that must never be missed belongs in memory (§14c).

## System prompt changes

Proposed paragraph for `data/CommonSystemPrompt.txt` (shared by every agent, so children inherit it), verbatim:

```txt
Large intermediate findings belong in the shared note store, not in the conversation.
When you produce an artifact too big to keep resident (call graph, error table, long analysis), write it
with notes(operation="write", ...) and keep only its key and one-line summary in front of you.
Never restate a long body to the parent agent: deposit it as a note and report the key instead.
Read the note index at the start of each turn; if you are handed a key, read it rather than asking.
When delegating, pass keys, not pasted note bodies. Name keys <area>.<topic>[.<subtopic>]; a repeat writes over.
```

## Registration, testing, risks, and rollout

Proposed; none of this is implemented. No globbing, so every list below is a required manual edit.

**(a) Registration**

- `CMakeLists.txt:46-55` header block: add `src/Tools/notes.h`; `CMakeLists.txt:32-44` core block: add
  `src/Notes.cpp` and `src/Notes.h`.
- `Tools::notes::def` into `MainTools[]` (`src/main.cpp:42-47`), `src/Tools/research.h:21` and
  `src/Tools/implement.h:25`.
- `#include <Notes.h>` in `src/Notes.cpp`, `src/main.cpp`, `src/Tools/notes.h`, and `src/ToolUtilities.h` (for
  the `shared_ptr` member); `#include "notes.h"` beside the other quoted includes in `research.h`/`implement.h`.

**(b) Testing** — `tests/NotesTest.cpp`, listed in `tests/CMakeLists.txt` like its siblings
(`target_include_directories(... ../src)`, `add_test`), ~7 cases:

- write/read/erase round-trip returns byte-identical bodies and the right `entries` count.
- overwrite returns the previous size in `replacedBodyBytes` and leaves no duplicate entry.
- `unknown_key` for an unwritten key and for an erased one.
- `Index()` is byte-stable and sorted (# entries ascending by key regardless of insertion order).
- truncation emits the `+N more` footer and stays within 1500 chars.
- a note written through a child's context copy is readable through the parent's (the shared-`shared_ptr` test;
  a value-copied store fails it, which is the point).
- a 65537-byte body is rejected with `too_large` and leaves the store unchanged.

**(c) Risks**

- Index bloat: 60 entries fit, 120-char summaries do not; the budget belongs in the store, not the prompt.
- Key collisions across subagents: two children writing `analysis` overwrite each other; nothing prevents it.
- Staleness: terse notes about files are trusted exactly because they are terse, and go wrong once changed.
- Misuse as memory: a note that must never be missed will be, since nothing guarantees an index read.

**(d) Rollout**

- Phase 1: `write`/`read`/`list`, one shared store, transient index per turn, no persistence, no erase.
- Phase 2: `erase`, the entry/body/index caps, `index_full`.
- Phase 3: `--notes-file` persistence and provenance reporting in `list`.
