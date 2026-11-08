# Context compaction design

Status: **proposal, nothing implemented.** Every symbol, file and flag below that does not appear in
the "grounded facts" list is explicitly marked *proposed*.

Related designs: [02 Memory tool](02-memory-tool.md) (a model-authored always-resident digest, which compaction must pin), [03 Session reset](03-session-reset.md) (wholesale amnesia, the complement to elision), [04 Shared note store](04-shared-note-store.md) (out-of-context bodies whose index is resident and pinned), and [05 Memory and context programme](05-memory-and-context-programme.md) (coordinates the four designs).

## Problem

`Session::Pimpl` keeps the entire conversation in `std::vector<ConversationMessage> m_conversationHistory`
(`src/Session.cpp:943`). It is seeded with the system message in the Pimpl constructor
(`src/Session.cpp:417`), and `Prompt` (`src/Session.cpp:912`) only ever *appends* to it: the
assistant message that carries `tool_calls`, then one `role="tool"` message per call
(`src/Session.cpp:812-825`). Nothing is ever trimmed, nothing in the conversation is persisted, and
there is no context-length limit and no tokenizer; the session recorder keeps per-turn token usage
and wall time as evidence (`src/SessionRecorder.h:48`), but nothing in the agent loop reads them
back. A long agent run therefore grows the log without bound.

The log is not just large, it is *redundant*:

| Redundancy class | How it arises in the current loop |
| --- | --- |
| Stale file content | `read_file_chunk` -> `edit_file_lines` -> `read_file_chunk` of the same region: the pre-edit copy is dead weight |
| Duplicate queries | the same `glob` or `grep` issued again after an edit, or issued twice with identical arguments |
| Superseded build output | `build` succeeded at turn 4, failed at turn 7, succeeded again at turn 9: turns 4 and 7 are noise |
| Whole-file vs. chunk overlap | `read_file` of a file whose later `read_file_chunk` covered the only edited region |

Prefix caching argues for keeping the log append-only: a provider that caches the prompt prefix bills
and processes only the appended suffix per turn, so every *unchanged* leading message is cheap. Compaction
is therefore a **deliberate, cache-invalidating trade**: the moment a mid-history message is rewritten,
every token after it must be re-prefilled. It must consequently be rare, threshold-triggered, and
amortized over enough remaining turns to be worth it.

Cost model: one compaction re-processes the whole surviving prefix once, i.e. roughly `N` prefill
tokens, where `N` is the pre-compaction prompt size. If compaction reclaims `R` tokens per subsequent
turn and `T` turns remain, it pays for itself only when

```
R * T  >  N        (the prefill cost of the single invalidating pass)
```

Because `T` is unknown, the design uses a conservatively *large* reclaim threshold (§7 `minReclaim`)
rather than estimating `T`; the intent is that compaction happens a handful of times per session, not
every turn.

## Hard constraint: message shape

OpenAI-style chat-completions dialects (the dialect emitted by `BuildPayload`, `src/Session.cpp:101-148`)
require a strict pairing: for every assistant message whose `toolCalls` array is non-empty, each entry's
`id` must be answered by exactly one `role="tool"` message carrying that same `tool_call_id`, in order.
Deleting or reordering messages therefore breaks the wire protocol, not merely the model's expectations.

Consequences, which are non-negotiable for this design:

1. Compaction is **elision**, implemented by rewriting the message's `content` string. Messages are never
   erased from `m_conversationHistory` and never reordered.
2. The system message is never touched: mutating it would invalidate the cache for the *entire* prefix,
   including turn 0, and the `maxTokens` paragraph appended in the constructor (`src/Session.cpp:409-417`)
   assumes immutability.
3. `toolCallId` and the `toolCalls` array are *preserved verbatim*; only `content` shrinks (§6).
4. `ConversationMessage` (`src/Session.cpp:93-99`) is a flat struct of `role`, `content`, `toolCallId`,
   `toolCalls` and has no id or extra fields today. Compaction needs bookkeeping (stable identity for
   logging and idempotence, provenance for supersession, protected-suffix bookkeeping), so metadata has
   to be **added** to the struct (§4). Because `content` is a plain `std::string` with no content-parts
   variant, there is no second place to hide state: all of it goes in the struct.

## Proposed data model

Proposed additions to `Session::Pimpl::ConversationMessage` (`src/Session.cpp:93-99`); all defaulted so
existing aggregate initialisation of the seeded system message and of `BuildToolResponseMessage`
results keeps working:

```cpp
// proposed: src/ContextCompaction.h
struct ResourceKey
{
    std::string tool;      // producing tool name, e.g. "edit_file_lines"
    std::string subject;   // normalized path, normalized pattern, or a constant
    std::string variant;   // flags / root_dir / line range class; may be empty
    std::string Dump() const;   // canonical "tool|subject|variant", used for equality + logging
    bool operator==(ResourceKey const&) const = default;
};

// proposed: additions to ConversationMessage
std::uint64_t id            = 0;      // monotonic, assigned at push time; 0 = never assigned
std::uint32_t turn          = 0;      // value of the turn counter when the message was appended
std::optional<ResourceKey> resourceKey;   // engaged only for keyed tool results
std::uint32_t elidedBytes   = 0;      // > 0 marks the message as a stub; makes stubbing idempotent
bool          pinned        = false;  // never elided
```

`turn` is a proposed new counter in `Session::Pimpl`, incremented once per iteration of the agent loop
in `Prompt`; it is also the value written into `response.json` bookkeeping so logs and stubs agree.
`id` is proposed to be assigned by a single `AppendMessage(...)` helper that replaces the ad-hoc
`m_conversationHistory.push_back(...)` calls at `src/Session.cpp:812-825` and in the tool-call branch.

Key derivation per tool (proposed; the handlers in `src/Tools/*.h` know their own arguments, so the
derivation belongs next to `BuildToolResponseMessage`):

| Tool | `tool` | `subject` | `variant` | Notes |
| --- | --- | --- | --- | --- |
| `read_file`, `read_file_chunk` | tool name | workspace-relative normalized path | requested line range, normalized (`"1-200"`) | range is part of the key: a second chunk of the same file does not supersede the first |
| `write_file` | `write_file` | normalized path | `""` | whole-file rewrite |
| `edit_file_lines` | `edit_file_lines` | normalized path | `""` | whole-file supersession, see §5 |
| `glob` | `glob` | normalized `pattern` | normalized `root_dir` | |
| `grep` | `grep` | normalized `pattern` | normalized `file_path` + sorted flag set | |
| `build` | `build` | constant `"build"` | `""` | one build stream per session |
| `research`, `implement` | tool name | `std::to_string(hash(prompt.substr(0, 80)))` | `""` | the prompt prefix is the only stable identity available |
| memory / recall tools | — | — | — | **unkeyed**: never superseded, only pinned or left alone |

"Normalized" means lexically-normalized, workspace-relative, forward-slash separators, with the same
normalization already applied by the `ValidatedFileSystem` in `ToolsRuntimeContext.fs`
(`src/ToolUtilities.h:56-67`). `Dump()` is the canonical form: it is what gets compared, what gets
written into the compaction log, and what a stub prints (§6).

Subagent sessions receive a *copy* of `ToolsRuntimeContext` (`src/ToolUtilities.h:56-67`), so their
resource keys are computed against the same workspace root but their message ids live in their own
`Session::Pimpl`. Keys are therefore only compared inside one session; the id spaces must not be merged.

## Supersession rules

Deterministic policy, evaluated in one pass over the non-protected history (§9). Rules are applied
newest-first so that "keep the newest of a duplicate class" is trivially local.

| # | Rule | Action | Rationale |
| --- | --- | --- | --- |
| R1 | Two messages have equal `ResourceKey::Dump()` | All but the newest become stubs | The newest answer already contains everything the model needs; the older copies are strictly less current |
| R2 | A `read_file`/`read_file_chunk` of path `P` is followed by a successful `edit_file_lines`/`write_file` of `P` | The read becomes a stub | This is the "multiple versions of the same file" case that dominates long runs |
| R3 | Repeated identical `glob` / `grep` (equal key) | Keep the last only | Identical result for identical arguments unless the tree changed; if it changed, R3's newest is the right one |
| R4 | Older `build` results | Keep the last build in full. For an older *failing* build, keep only the first 8 and last 8 lines of its output, marked as truncated | A compiler error that was later fixed is history; a compiler error that is *still* present is the last one and stays intact |
| R5 | Any unkeyed message (memory/recall tools, free-form assistant text) | Never stubbed by supersession | No safe identity, and these are exactly the messages that carry durable decisions |
| R6 | Any message inside the protected suffix (§9) | Never stubbed | The model is actively working on it |
| R7 | Any message whose `content.size() < ~200` bytes | Never stubbed | The stub text would be nearly as large as the payload; the reclaim does not pay |

Rules are *not* transitive across turns: a message stubbed by R2 is not re-examined for R1. Since
`elidedBytes != 0` marks a stub, a second compaction pass over an already-stubbed message is a no-op
(§13, idempotence test), and the reclaim from a stub is never counted twice.

## Stub format

A stub is plain text placed inside the existing `content` string of the `role="tool"` message. Example:

```
[compacted read_file_chunk: path=src/Session.cpp, lines 371-430, 2.4 KB elided; superseded by turn 12]
```

Grammar (proposed, one line, no trailing newline):

```
[compacted <tool>[ <selector>]: <subject>, <selector-detail>, <size> elided; superseded by turn <N>]
```

Requirements:

- `toolCallId` is copied unchanged from the original message, and the message keeps its index. Only
  `content` changes — this is what keeps the protocol pairing of §3 valid.
- `content` stays a JSON **string**. `BuildPayload` dumps `content` directly, and some providers reject
  non-string `tool` content (arrays of content parts are not universal). No structured stub object is
  emitted in the payload; structured detail goes to the compaction log (§12) instead.
- The stub must be self-describing enough that the model can decide to re-read: it names the file,
  the range and the superseding turn, which is exactly the information needed to issue a fresh
  `read_file_chunk` if the stub turns out to be insufficient.
- For R4 (truncated old build output) the stub is the retained head/tail plus a
  `... [N lines elided; superseded by turn 12] ...` marker inside the same string.

## Trigger and scheduling

Proposed `ContextBudget` (new file `src/ContextCompaction.h`):

| Field | Default | Meaning |
| --- | --- | --- |
| `contextWindowTokens` | from service, `0` = unknown => compaction disabled | size of the provider's context window |
| `highWaterMark` | `0.75` | compact only above this fraction of the window |
| `lowWaterMark` | `0.55` | compact *down to* this fraction, not to the mark, to avoid thrashing |
| `minReclaim` | `4 KB` or `5%` of the window, whichever is larger | projected reclaim must exceed this |

`contextWindowTokens` is **new information** that does not exist today: there is no agent config file
in the repository, so there are two proposed sources, in priority order:

1. A new `contextWindowTokens` field on `ServiceDefinition` (`src/Services.h:14-22`) next to the existing
   `maxTokens`, parsed out of the user-level `services.json` by the existing
   `ParseServicesConfig` (`src/Services.h:33`) and surfaced into `Session::Config` the same way
   `maxTokens` is (see the constructor paragraph at `src/Session.cpp:409-417`).
2. A proposed `--context-window <tokens>` CLI flag in `src/CommandLine.cpp`, overriding the service
   value, for services whose window the config does not know.

Token estimation without a tokenizer. The design keeps a `double bytesPerToken` calibration factor,
initialised to `3.6`:

```
estimated(m) = 4 + ceil(content.size() / bytesPerToken) + sum over toolCalls of (4 + ceil(dumped.size() / bytesPerToken))
promptEstimate = sum over messages + payload overhead (tools schema dumped once, ~ceil(n/3.6))
```

The `+4` per message accounts for role/name framing tokens. When a response body carries
`usage.prompt_tokens`, that value is compared with the estimate made for the same request and the factor
is updated (exponentially weighted, clamped to e.g. `[2.5, 5.0]`), then **stored and reused** for the
rest of the session. Providers that omit `usage` simply leave the last known factor in place.

Scheduling rule:

```
if contextWindowTokens == 0:                        return;   // nothing to budget against
if promptEstimate <= highWaterMark * window:        return;   // rare by construction
projected = promptEstimate - estimateAfterStubbing();
if projected < minReclaim:                          return;   // wouldn't pay for the prefill
compact();                                          // stop stubbing once <= lowWaterMark * window
```

## Integration point

`MaybeCompact()` is called in `Session::Pimpl::Prompt` **immediately before `BuildPayload`**, at the top
of each turn — that is, between the previous turn's batch of tool-result messages and the next request.
It is never called mid-turn and never between an assistant `tool_calls` message and the `role="tool"`
messages answering it, because that interval has no valid wire representation (§3).

```cpp
// proposed: src/ContextCompaction.h
namespace ContextCompaction {
struct Options { ContextBudget budget; unsigned minProtectedTurns = 3; bool enabled = true; };

struct Result {
    bool   compacted = false;
    std::size_t bytesBefore = 0, bytesAfter = 0;
    std::size_t estimatedTokensBefore = 0, estimatedTokensAfter = 0;
    std::vector<std::uint64_t> stubbedIds;
    std::vector<std::string>  rulesApplied;   // "R2:x3", "R3:x1", ...
};

Result MaybeCompact(std::vector<Session::Pimpl::ConversationMessage>& history,
                    unsigned currentTurn, Options const& opts);
} // namespace ContextCompaction
```

```cpp
// proposed: inside Session::Pimpl::Prompt, top of each turn's loop body
while (turn < maxTurns)
{
    if (m_contextBudget.enabled)
    {
        auto r = ContextCompaction::MaybeCompact(m_conversationHistory, turn, m_compactionOptions);
        if (r.compacted)
            WriteCompactionLog(r, turn);   // §12
    }

    auto payload = BuildPayload(hurryMessages);   // unchanged signature, src/Session.cpp:101-148
    ...
}
```

The `hurryMessages` escalation (`src/Session.cpp:831-883`) is unaffected: those are transient, are passed
to `BuildPayload` and are never stored, so compaction never sees them — the same precedent that makes a
non-persisted trailing message legal at all.

New files, and the exact CMakeLists.txt change (the file list is explicit, no globbing; the entry goes
next to `src/Session.cpp` in the `add_executable(kern ...)` list at `CMakeLists.txt:32-58`):

```cmake
    src/ContextCompaction.cpp   src/ContextCompaction.h
```

i.e. inserted as a new line directly above `src/Session.cpp         src/Session.h`. Nothing else in
CMakeLists.txt changes for Tier 1.

## Protected suffix and pinning

Never compacted, in any tier:

- the system message (index 0, seeded at `src/Session.cpp:417`);
- the last `minProtectedTurns` turns (default `3`), by `turn` value, not by message count — a single
  turn can contain many tool messages and truncating "the last N messages" would slice a batch in half;
- memory-digest messages (unkeyed by §4) — they are the durable summary and stubbing them loses
  information that exists nowhere else;
- any message with `pinned == true` (proposed: the model or the user may pin; pinning is honoured
  unconditionally).

Keys are **recomputed over the whole non-protected history on every pass**, not cached incrementally:
`ResourceKey` depends on normalized arguments that repeat across the history, and a stale key map would
mis-supersede after an edit. The recompute is O(history) string work, negligible against one prefill.

Note the asymmetric interaction with §11: the protected suffix is a *moving window*, so a message that
was protected at turn 9 becomes eligible at turn 13. That produces a second, smaller cache
invalidation later — accepted, because the alternative (freezing everything ever protected) reclaims
nothing.

## Tier 2: model-assisted digest (optional, off by default)

Tier 1 stubs are purely mechanical. When a *cluster* of adjacent elided messages is still large after
Tier 1, an optional Tier 2 pass may issue **one extra non-streaming completion** asking the model to
summarise the cluster into a short digest that keeps decisions ("we chose X because Y"), and store that
digest as the `content` of one designated message in the cluster (the rest stay Tier 1 stubs). The
digest completion can reuse `createNewSession` from `ToolsRuntimeContext` (`src/ToolUtilities.h:56-67`)
with a restricted tool set (e.g. no tools at all) rather than inventing a new HTTP path.

Constraints:

- **off by default**; enabled only by a proposed flag (see §12);
- costs one request and one extra prefill of the cluster, so it applies only when
  `minReclaim` would otherwise be missed;
- must **never** run inside a turn that is already retrying (the escalation path at
  `src/Session.cpp:831-883`), because a retry plus a digest request compounds latency and cost;
- must **degrade gracefully**: any transport error, non-200, malformed body or empty completion leaves
  the Tier 1 stubs exactly as they are — Tier 2 is strictly additive and never a precondition;
- the digest request/response pair is logged under the same per-turn convention as a normal request.

Tier 1 is deterministic, pure and therefore unit-testable (§13). Tier 2 depends on a live model, is
non-deterministic, and can only be tested with a fake service or an injected completion callback —
which is precisely why it is not in scope for the first three phases (§15).

## Interaction with prefix caching

| Mutation | Cache effect | Design response |
| --- | --- | --- |
| Append a message | Free: the cached prefix is untouched, only the new suffix is prefilled | Normal operation; compaction must not interfere |
| Stub a mid-history message at index `i` | Everything after `i` is invalidated; tokens `0..i` survive | Accept, but do it in one pass |
| Stub several messages at a contiguous region | Same as stubbing the *first* one in the region | Batch them: rewrite the region in a single pass |
| Change the system message | Everything invalidated, from token 0 | Forbidden (§3, §9) |
| Reorder / delete | Wire protocol break, total invalidation | Forbidden (§3) |

Therefore: a compaction pass rewrites a **contiguous prefix region bottom-up in a single pass**, so the
cache boundary moves exactly once per pass. Rewriting bottom-up means the invalidation point is the
*earliest* rewritten message; rewriting top-down would produce the same final boundary but invites
per-message request interleaving bugs. Multiple compaction events in a session are therefore multiple
boundary moves, and the trigger policy (§7) is chosen to keep that count small.

## Observability

Reuse the existing convention: `WriteLogFile`/`BuildLogPath` already write
`logs/<8char-id>_<NNN>_{request.json,response.json,response_raw.txt}` next to the executable. A
compaction event writes `logs/<8char-id>_<NNN>_compaction.json` with the same id and turn number as the
request it precedes:

```json
{
  "turn": 12,
  "estimatedTokensBefore": 118400,
  "estimatedTokensAfter": 61200,
  "bytesBefore": 421330,
  "bytesAfter": 217904,
  "contextWindowTokens": 128000,
  "highWaterMark": 0.75,
  "lowWaterMark": 0.55,
  "minReclaim": 6144,
  "calibrationBytesPerToken": 3.62,
  "rulesApplied": [{"rule": "R2", "count": 5}, {"rule": "R3", "count": 2}],
  "stubbed": [
    {"id": 41, "turn": 4, "tool": "read_file_chunk", "key": "read_file_chunk|src/Session.cpp|371-430", "elidedBytes": 2410}
  ],
  "tier2": {"attempted": false}
}
```

Plus one console line, matching the existing terse console style:

```
compaction turn 12: 118400 -> 61200 tokens (-48%), 7 stubs (R2 x5, R3 x2)
```

Proposed new flags in `src/CommandLine.cpp`: `--no-compaction` (hard disable, honouring §7's
`contextWindowTokens == 0` path) and `--tier2-digest` (opt into §10).

## Testing

Proposed `tests/ContextCompactionTest.cpp`, registered in `tests/CMakeLists.txt` following the existing
explicit-list pattern — a new `add_executable` listing the test source, `target_include_directories(... ../src)`,
linking only what the Tier 1 code needs (no network), and `add_test`. Tier 1 must not need a live service,
which is why it lives in a `{h,cpp}` pair with no HTTP dependency.

| Test | Given | Expect |
| --- | --- | --- |
| `ReadThenEditSupersession` | read `src/Session.cpp` (turn 2), edit same path (turn 3) | turn-2 tool message becomes a stub; turn-3 untouched; ids preserved |
| `DuplicateGrep` | two identical `grep` results (turns 3, 8) | first stubbed, second intact |
| `ToolCallIdPreserved` | any stubbed message | `toolCallId` byte-identical; `toolCalls` of the assistant message untouched |
| `ProtectedSuffixUntouched` | edit at turn 10, `minProtectedTurns = 3`, compaction at turn 12 | no message with `turn >= 10` modified |
| `NoOpBelowThreshold` | estimate at 40% of window | `Result.compacted == false`, history byte-identical |
| `StubIdempotence` | run `MaybeCompact` twice at the same turn | second pass reports 0 stubs; `bytesAfter` unchanged |
| `UnkeyedMessagesUntouched` | unkeyed messages among keyed duplicates | none stubbed, none pinned implicitly |
| `MonotonicTokenEstimate` | estimates over a growing history | non-decreasing; `usage.prompt_tokens` calibration converges and is retained |

`BuildPayload` output should also be asserted on in one test: after compaction the payload still
parses, still has a `tool_call_id` per `role="tool"` message, and `messages` has the same length.

## Failure modes

| Failure | Symptom | Mitigation |
| --- | --- | --- |
| Provider rejects rewritten tool content | 400 on the next request | Stubs are plain strings (§6); keep stub text short; a hard-error path that restores the previous `content` if a request fails after compaction (proposed) |
| Cache thrash from too-frequent compaction | cost *rises*; every turn re-prefills | Hysteresis (`highWaterMark` -> `lowWaterMark`), `minReclaim`, and the `R*T > N` argument in §2 |
| Eliding the only copy of an important error | model repeats a mistake or re-runs a failing build | R4 keeps old failing-build head/tail; unkeyed messages never stubbed (§5 R5); pinning (§9) |
| `ResourceKey` collisions across subagent prompts | two different `implement` calls with the same 80-char prefix collide | Hash the 80-char prefix *and* keep `tool` in the key; keys are compared only within one session's history |
| Estimate drift when a provider omits `usage` | factor never recalibrates; compaction fires too late | Clamp the factor; fall back to the conservative `3.6`; a provider-side hard context error is logged as evidence the estimate was low |
| Compaction interacting with `finish_reason == "length"` | double shrinking in one turn | `MaybeCompact` runs before `BuildPayload` only; `hurryMessages` stay transient |

## Open questions

1. Should `id`/`turn` live in `ConversationMessage` or in a parallel vector? A parallel vector keeps the
   struct exactly as it is today but makes interleaved indexing a new bug class. Leaning struct.
2. What is the right `bytesPerToken` for tool-call argument JSON, which is unusually punctuation-dense?
   Needs Phase 0 measurement, not a guess.
3. Should compaction refresh *file-backed* stubs when the file on disk has changed since the stub (i.e.
   a stub of path `P` re-expanded because the model now reads `P` again)? Tempting, but it makes
   compaction stateful across turns; deferred.
4. Do any target providers actually reject multi-message rewrites (e.g. specific KV-cache validation)?
   Unknown without measurement.
5. Should `--context-window` also accept `auto` and probe the provider? No such probe exists today.
6. Is a per-message byte budget (`maxBytesPerToolResult`) a better primary lever than supersession?
   Supersession is semantically safe; a byte cap is not.

## Phased rollout

| Phase | Scope | Risk | Exit criterion |
| --- | --- | --- | --- |
| 0 | Metadata only: add `id`, `turn`, `resourceKey`, `elidedBytes`, `pinned`; compute keys; log redundancy to `logs/<id>_<NNN>_compaction.json`; **no elision ever** | Very low (no behaviour change to the payload) | Real sessions show what fraction of tokens is redundant, and `bytesPerToken` is measured |
| 1 | Tier 1 deterministic stubs + supersession rules R1-R7, still manual/threshold-free and exercised only from tests and a hidden flag | Low | Unit tests green; a stubbed payload is accepted by every supported provider |
| 2 | `ContextBudget`, `contextWindowTokens` on `ServiceDefinition` + `services.json`, `--context-window`, `--no-compaction`, automatic trigger with hysteresis | Medium (cost/latency) | Compaction frequency and reclaim measured against the §2 payoff inequality |
| 3 | Tier 2 model-assisted digest, off by default, restricted tool set via `createNewSession`, graceful degradation | Medium-high (extra requests, non-determinism) | Measurably better than Tier 1 alone on long runs; no increase in failed requests |
