# Session recording design

Status: proposal, nothing implemented. Recording in Kern today is diagnostics only: per-turn files written so that a human can inspect a run after it has already gone wrong. This plan makes every run produce a record complete enough to replay, and it is the prerequisite for [07 Evaluation harness](07-evaluation-harness.md).

Related designs: [07 Evaluation harness](07-evaluation-harness.md) (which consumes recorded transcripts as its only input), and [08 Evaluation programme](08-evaluation-programme.md) (which defines the change classes a transcript must be able to discriminate); each of the two links back to this plan.

## The problem

Per-turn artifacts are written beside the executable under `logs/`, a directory produced by `Session::Pimpl::GetLogsDirectory()` (`src/Session.cpp:316`) and joined to a filename by `Session::Pimpl::BuildLogPath()` (`src/Session.cpp:326`). The filename shape is `<8-char id>_<NNN>_<name>.<ext>`, where the id comes from `GenerateLogId()` (`src/Session.cpp:56`) and the sequence number from the `LogFileState` (`src/Session.cpp:74`) member `m_log`. Writes go through `WriteLogFile()` (`src/Session.cpp:80`), which opens the target with `std::ios::trunc` and throws a `std::runtime_error` when the open fails, so a single unwritable path aborts the run it was merely supposed to describe.

What those files contain is thin. Token usage is formatted into the debug log at `src/Session.cpp:208` and never reaches a file, so cost is visible only to someone watching the console. The raw response body is dumped as `<id>_<NNN>_response_raw.txt` only when the debug level is active (`src/Session.cpp:403`), which means the most faithful record of what the service actually said exists only in the runs that were already being debugged. The system prompt has no artifact of its own: it is recoverable only by re-reading `messages[0]` of a request payload. There is no session manifest, no start or end marker, no timings, no model snapshot id, no tool-schema identity, and no workspace identity. The id itself is random, so nothing links a log directory to the task it was serving or to the change that was being tested.

The consequence is concrete. The existing logs cannot answer the question that motivates the whole evaluation effort — which of these forty runs did the tool-schema change break — because the schema is nowhere in the record and the runs are not labelled by anything but an opaque id. Nor can they be replayed: replay needs the tool definitions, the prompt text, the turn limit and the decoding parameters, and those are precisely the inputs that drift between one run and the next. Recording is therefore not an observability nicety but the substrate on which any comparison rests.

There is a further cost that is easy to overlook. Because the id is regenerated on every launch, two runs of the same task on the same day are as unrelated in the record as two runs on different machines, and a directory listing of `logs/` is a list of anonymous bundles rather than a history. The only link back to intent is the console output, which is not kept.

The goal is one sentence: every run should produce a self-contained, redacted, replayable record by default.

## What a transcript must contain

| Field group | Contents | Why it must be captured |
| --- | --- | --- |
| Identity | Session id, parent id and role, transcript schema version, start and end timestamps | A record with no stable identity cannot be selected, joined to its children, or cited by a corpus case. |
| Provenance | Kern build id, model name, pinned snapshot id, endpoint host, decoding parameters, `maxTurns`, `maxTokens` | Without it, two runs cannot be told apart, and a difference in score cannot be attributed to a change. |
| Inputs | System prompt text with its sha256, tool definitions JSON with its sha256, user prompt | These are the two variables evaluation exists to compare, and both must be byte-identical to what the model saw. |
| Per-turn I/O | Request payload, response message, assistant text, finish reason | The turn is the unit of failure, so a replay must be able to stop and restart at any of them. |
| Per-turn usage and timing | Prompt and completion tokens, wall time per request | Cost and latency regressions are first-class results and are otherwise unrecoverable after the fact. |
| Tool execution | Name, arguments, result, error code, duration | The tool call is where the agent's behaviour lives; a transcript without it records talk, not action. |
| Outcome | `Session::PromptResult` fields and a completion flag | The verdict of the run must live in the record, not in the caller that consumed it. |
| Workspace delta | Files read, files written, commands run, build status | This is what a check asserts against, and it cannot be reconstructed from message text. |

The provenance group is the one the current logs lack most damagingly, and it must carry the Kern build identity, the model name and its pinned snapshot id, the endpoint host with no credential material, the decoding parameters actually sent, and both `maxTurns` and `maxTokens`. A record that omits any of these is a description of a run, not a run: it cannot be reproduced, only imitated. The endpoint host is included because a local and a remote deployment of the "same" model are not interchangeable inputs.

The input group must store the system prompt text together with its sha256, and the exact tool definitions JSON together with its sha256. Prompts and schemas are the two things evaluation exists to compare, and both are the most likely to be edited between runs without anyone noticing; a hash makes the difference visible even when the text is long enough that a reader would not spot it. Storing the text as well as the hash is deliberate, because the hash selects a case and the text replays it.

The division between the two groups is exact and must not be read either way round. The system prompt sha256 and the tool-definitions sha256 belong to the `inputs` group, and nothing else in the manifest carries a hash. The `provenance` group holds the build identity, the model name, the pinned snapshot id, the endpoint host with no credential material, the decoding parameters, `maxTurns` and `maxTokens`, and it holds neither hash. That split is why [07 Evaluation harness](07-evaluation-harness.md) has to merge `inputs` and `provenance` when it populates a corpus case's provenance object.

The outcome group costs nothing new. `Session::PromptResult` already serializes itself through `operator nlohmann::json()` (`src/Session.cpp:741`), producing `ok`, `errorCode`, `content` and `turnsUsed`, so the recorder only has to place that object in the manifest.
The workspace delta group turns a transcript from a conversation into evidence, because it is the one group a check can be written against without parsing model prose. Files read and written are already visible to the tools through `ToolsRuntimeContext::fs` (`src/ToolUtilities.h:59`), and the commands run and the build status arrive as tool results, so the delta is derived rather than measured a second time. A delta that names paths is a delta a reviewer can later assert on.

Two omissions cut across the groups. The first is that nothing records what the recorder itself did — whether it wrote, warned, or was switched off — so the manifest's own presence is the only evidence of its success until that is added. The second is that timing is grouped with usage rather than kept separately, because a latency regression and a token regression arrive together far more often than either arrives alone.

Two cross-cutting decisions belong to this table. The request payload is stored once per turn rather than once per session, because a replayed turn must see the history as it stood at that turn and not as it ended. And every field a later reader might need to compare is stored as a value, never as a pointer to one, so a transcript remains readable after the process that wrote it has exited and the model name it references has been retired.

## The storage decision

The flat per-turn files stay. Other plans already reference the `logs/<id>_<NNN>_*.json` naming, and continuity of the debugging artifact is worth more than a tidy scheme. Two new artifacts are added per session, and only two. The first is `<id>_session.json`, a manifest written once at session start and rewritten atomically at session end, holding identity, provenance, inputs and outcome. The second is `<id>_turns.jsonl`, one appended line per turn, carrying usage, timing, finish reason, tool calls and tool errors.

The split is driven by two properties rather than by taste. The manifest is written twice precisely so that a crashed run still leaves a readable identity: the start version carries everything known before the first token, and the end version adds the outcome and flips the completion flag. The jsonl file is append-only, so a long session never re-serializes the whole history to add one turn, and a truncated final line is the only damage a crash can do.

A structural change is required. `Session::Pimpl` (`src/Session.cpp:731`) currently keeps only `m_endpointDescriptor`, `m_toolContext`, `m_modelName`, `m_tools`, `m_maxTokens`, `m_conversationHistory` and `m_log`. It gains the small recorder object and, through it, the provenance it cannot currently see: the build id, the decoding parameters and the transcript schema version. None of that is derivable from the members that exist today, which is the same point as the provenance row above, expressed as a class change.

`RawWriteTextFile` (`src/FileUtilities.h:23`) is the atomic writer already used elsewhere in the codebase, and `WriteLogFile` (`src/Session.cpp:80`) is not, so the manifest uses the former. The proposed top-level shape:

```cpp
// proposed: <id>_session.json
{ "schemaVersion", "complete", "identity", "provenance", "inputs", "outcome", "workspace" }
```

## Recording is best effort, replay is not

A failed transcript write must warn and continue. Recording is instrumentation, and instrumentation must never turn a working run into a failed one; a full disk or a read-only session directory is a reason to log a warning, not to abandon a task that was making progress. This is the opposite of the current behaviour of `WriteLogFile`, which throws, and it is the reason the manifest does not reuse it.

Completeness, however, is a different promise. A transcript that claims to be replayable must be complete, so the manifest carries `"complete": true|false`, and a transcript with `"complete": false` is refused by the harness rather than silently scored. A partial record is still useful evidence; it is simply not a case.

The redaction invariant holds throughout. Credential material is never recorded, `$HOME` and all absolute paths are rewritten relative to the session root, and the output of tools that touch secrets is recorded as-is only when it already passes the existing redaction path, which is the key-stripping at `src/NetworkUtilities.cpp:163-167`, where `ReplaceIn` rewrites the endpoint's API key to `[redacted]` before it is assigned to `HttpResult::body` (`src/NetworkUtilities.h:9`, the field itself). The recorder adds no second redaction mechanism, because two mechanisms drift apart.

The split between the two promises is what keeps the design honest. Best effort governs the act of writing, so a failure is reported and the task continues; completeness governs the claim the file makes about itself, so no reader has to guess whether silence meant "nothing happened" or "the writer could not reach the disk". A record that failed to write leaves the run unrecorded and the run's own success untouched, which is the correct ordering of concerns.

## Control surface

| Addition | Where it lands | Effect |
| --- | --- | --- |
| `--record on\|off\|auto` | `CommandLineOptions` (`src/CommandLine.h:30`) | Default `auto`: on for interactive runs and for `research` and `implement` subagent sessions, off for a bare scripted invocation. |
| `--session-dir PATH` | `CommandLineOptions`, consumed where `Session::Config` is built | Overrides the directory derived from `GetLogsDirectory()` so transcripts can live outside the build output. |
| `--session-tag TEXT` | `CommandLineOptions`, written to the manifest's top-level `"tag"` string field | A free-form label such as `tool-schema-v3` that the harness uses to select runs. |
| `showSessions` | `enum class CommandLineAction` (`src/CommandLine.h:21`) | Prints a table of recorded sessions with their tags, model and completion flag. |

Subagent sessions must be recorded under the parent's session id with a role field, so a `research` or `implement` run appears as a nested child rather than as an unrelated top-level transcript. The subagent tool bundles are defined in `src/Tools/research.h` and `src/Tools/implement.h`, and sessions are created through `ToolsRuntimeContext::createNewSession` (`src/ToolUtilities.h:58`), a callable member assigned by the caller that constructs the context (`src/main.cpp:142`) and forwarded by those bundles, which is the single place the parent identity can be handed down without touching either tool.

The defaults are chosen so that the useful record happens without anyone remembering a flag. An interactive run has a human present who will want the transcript if the run goes wrong, and a subagent session is the case where provenance matters most, because a child's tool list is a restriction of its parent's and a difference between the two is a difference worth measuring. A bare scripted invocation stays off by default only because it is the one case where the caller is already recording something of its own.

The tag is the control that makes the corpus selectable. It lands in the manifest as a top-level `"tag"` string field, empty when unset, and is copied from there verbatim into every report, so a batch of runs labelled `tool-schema-v3` can be retrieved as a set after the fact without depending on file timestamps or on the directory layout having been stable. This `tag` is a free-form label on a whole recorded run, and it is deliberately distinct from the `tags` array of capability labels on a corpus case defined in [07 Evaluation harness](07-evaluation-harness.md).

## The injection seam

`Session::Pimpl::Prompt` (`src/Session.cpp:371-728`) calls `HttpPost` (`src/NetworkUtilities.h:20`) directly. Neither replay nor a fake endpoint is possible while that call is hard-wired, because there is no point at which a recorded response can be substituted for a live one.

Proposed: `Session::Config` (`src/Session.h:28`) gains a member whose type is a new named alias declared exactly over the existing `HttpPost` signature — `HttpResult HttpPost(Endpoint const&, std::string_view payloadType, std::string_view payload, std::function<void(std::string_view)> const&)` (`src/NetworkUtilities.h:20`), with `HttpResult` (`src/NetworkUtilities.h:9`) as its result type — defaulted to `HttpPost` itself, so the call site needs no adapter and no wrapper. The alias is new code and must be introduced with this change; it does not exist today, and there is no prior wrapper to default to. The shape follows the callable members already carried by `ToolsRuntimeContext`. The call site in `Prompt` gains no branch, only an indirection.

This single seam is what makes recorded transcripts scoreable offline. It must be added before any harness work, because retrofitting it later invalidates every transcript recorded in the meantime: a record produced against a hard-wired transport cannot be guaranteed to have captured the same inputs a replay would supply.

Two properties of the seam matter for replay. It returns the whole `HttpResult`, including `status` and `error`, so a recorded transport failure can be replayed as a failure rather than being flattened into an empty body. And it is a value member of the configuration, not a global, so two sessions in one process can be driven by different transports, which is exactly what a harness scoring several cases concurrently requires.

What replay substitutes is a lookup: the case's transcript supplies the response recorded for a given turn, and the seam returns it without touching a socket. The substitute is a caller-side concern, and `Session.cpp` neither knows nor needs to know that its transport is recorded.

## From session to corpus case

Admission turns a recorded run into a versioned fixture, and it is a review, not a copy:

1. The recorder has already written a complete transcript, so no step here re-runs the model.
2. `kern_eval admit <session-id>`, from [07 Evaluation harness](07-evaluation-harness.md), copies the transcript into `eval/corpus/<case-id>/transcript.json`.
3. `case.json` is derived from the manifest: provenance, prompt hash and tool-schema hash become the case metadata rather than being re-typed.
4. `checks.json` is scaffolded from what the run actually did — files touched, commands run, build outcome — so the first draft of the assertions is a description of observed behaviour.
5. A `notes.md` placeholder is written for the reviewer.

The case is created with `"reviewed": false` and is never scored in a gating run until a human has reviewed it, set `reviewed_by` and `reviewed_at`, and either trimmed the transcript, for prompt-level cases, or pinned the fixture, for session-level cases. Admission is therefore cheap and gating is expensive, which is the correct ratio.

A case is admitted only if all of the following hold:

- It tests a capability intended to be kept, not an accident worth documenting.
- It is reproducible without network access or binaries that may be unavailable.
- It is mechanically scoreable, by a check that either passes or fails.
- It is plausibly sensitive to the change classes being measured.
- It contains no secret and no personal absolute path.

Conversely, a case is rejected if any of the following is true:

- Its transcript has `"complete": false`, so a replay cannot reproduce it.
- Its outcome depends on wall-clock timing, randomness, or a mutable remote service.
- Its checks assert narration rather than observable effects on the workspace.
- It duplicates an existing case at the same sensitivity without adding a distinct failure mode.
- It requires a fixture too large or too licensed to commit.

The rule behind both lists is that a case is a claim about intended behaviour, and the corpus is therefore a reviewed artifact under version control, not a dump of whatever the logs happened to collect. A dump grows monotonically and unreadably; a reviewed corpus is small, each entry defensible on its own, and a failing case is always a statement about something that was supposed to work.

Two consequences follow. The corpus must be small enough to run on every change that touches the prompt or the tool schemas, so admission is deliberately conservative and a case that only re-asserts an existing one is refused. And every case pins its provenance, so a case recorded against an older build still carries that build id in `case.json` and reports it when its score moves.

## Testing

The recorder itself gets hermetic CTest coverage with no network access: manifest and jsonl round-trip, redaction of absolute paths, refusal of a transcript marked `"complete": false`, and rejection of an unknown transcript schema version. It is added as a new executable in `tests/CMakeLists.txt` alongside the eight executables and their `add_test` calls already registered there (`tests/CMakeLists.txt:7-98`), following the hand-rolled `Expect`/`ExpectEqual` style in `tests/TestUtilities.h`.

One constraint shapes the file layout. `src/Session.cpp` is not currently compiled into any test executable, and compiling it would drag in the HTTP client and the whole tool runtime. The manifest writer and reader should therefore live in its own small translation unit that a test can compile alone, and `Session.cpp` should include that unit rather than own the serialization logic.

The round-trip assertion is deliberately byte-exact in both directions: a manifest written from a known configuration must read back to the same provenance values, and a jsonl file re-read line by line must yield the same turn count and the same per-turn usage. The versioning test belongs in the same executable, because a reader must fail loudly on a schema it does not know rather than populate fields that were never written. None of these tests constructs a `Session`, so none of them needs a service.

Registration follows the existing pattern exactly: the new translation unit joins the core block of the `kern` target in `CMakeLists.txt:31-58`, and the test joins `tests/CMakeLists.txt` with its own source list, `target_include_directories`, `target_link_libraries` and `add_test`.

## Failure modes

- A crash mid-session leaves `"complete": false` and an unanswered request in the last jsonl line, which the harness must treat as unscoreable rather than as a low score.
- A tag drifts into meaninglessness once nobody agrees what `v3` denotes, so the manifest's top-level `"tag"` string field is copied verbatim into every report alongside the provenance block.
- Transcripts recorded before the injection seam exists cannot be replayed and must be marked with an older schema version rather than silently accepted by a reader that has grown new defaults.
- Recording on a machine without a writable `--session-dir` must degrade to a warning and an unrecorded run, never to a failed task.
- A transcript recorded against a different Kern build is replayable for scoring, but its build id must be surfaced in every comparison report so that a cross-build difference is never read as a model difference.

## Open questions

1. Whether `logs/` and the session transcript should eventually be one directory: the per-turn files are debugging aids that expire with the build, the transcript is a record that outlives it. Leaning toward keeping them separate and having `--session-dir` default outside the build output.
2. Whether recording should default to on for every run, including scripted ones: storage is cheap and the lost record is unrecoverable. Leaning toward `auto` as specified, with `on` for interactive and subagent runs only.
3. Whether the recorder should hash the workspace tree at start: a tree hash gives cheap deduplication and detects a stale fixture, but hashing a large repository costs more than the record it guards. Prefers hashing only the files the session later touches.
4. Whether subagent transcripts should be nested inline in the parent's jsonl or kept as separate directories: inline keeps one file per session tree, separate directories keep each replay self-contained. Leaning toward separate directories with a parent id field, because replay granularity should match scoring granularity.
5. Whether the recorder should capture the full request payload per turn when the per-turn file already does: duplication is safe and removal is not. Deferred, pending a measurement of transcript size on a long session.
6. Whether `showSessions` belongs in the main binary or in the evaluation tooling: it reads only files the main binary wrote. Prefers the main binary, because the useful view is the one available immediately after a run.
