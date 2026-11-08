# Session recording design

Status: in progress. What remains is the `sendRequest` seam, the workspace delta, path relativisation and subagent nesting, all described below as pending work. A record complete enough to replay is the prerequisite for [07 Evaluation harness](07-evaluation-harness.md), which consumes those transcripts as its only input.

Related designs: [07 Evaluation harness](07-evaluation-harness.md) (which consumes recorded transcripts as its only input), [08 Evaluation programme](08-evaluation-programme.md) (which defines the change classes a transcript must be able to discriminate), and [09 Turn budget and session resumption](09-turn-budget-and-resumption.md) (which resumes a session from the transcript this plan defines); each of the three links back to this plan.

## The problem

Per-turn artifacts are written beside the executable under `logs/`, a directory produced by `Session::Pimpl::GetLogsDirectory()` (`src/Session.cpp:316`) and joined to a filename by `Session::Pimpl::BuildLogPath()` (`src/Session.cpp:326`). The filename shape is `<8-char id>_<NNN>_<name>.<ext>`, where the id comes from `GenerateLogId()` (`src/Session.cpp:56`) and the sequence number from the `LogFileState` (`src/Session.cpp:74`) member `m_log`. Writes go through `WriteLogFile()` (`src/Session.cpp:80`), which opens the target with `std::ios::trunc` and throws a `std::runtime_error` when the open fails, so a single unwritable path aborts the run it was merely supposed to describe.

The manifest and the turn log sit alongside those flat per-turn files, and between them they carry the identity of a run, its provenance, the inputs the model saw, the per-turn I/O, usage and tool execution with the arguments and results that were exchanged. A reader who wants to know which build, which model, which prompt and which schema produced a given answer need not reconstruct any of it from a console log that nobody kept.

What they still cannot do is narrower and worth naming exactly. They cannot be replayed, because `Session::Pimpl::Prompt` (`src/Session.cpp:912`) reaches the transport directly at `src/Session.cpp:560` and `src/Session.cpp:639`, so no recorded response can be substituted for a live one and a transcript can describe a run without being able to repeat it. They describe talk rather than action, because no workspace delta is recorded — no file read or written, no command run, no build status — and no path is rewritten relative to a session root, so the tool output a transcript stores is whatever bytes the tool produced. And a child session cannot be joined to its parent, because `parentId` is written into the manifest but never set.

There is a further cost that is easy to overlook. The id is regenerated on every launch, so two runs of the same task on the same day are as unrelated as two runs on different machines, and a directory listing of `logs/` is a list of anonymous bundles rather than a history. The transcript's tag and identity repair that for a single run, but not across runs: nothing joins a run to its children, so the record is still a flat list where it should be a tree.

The goal is one sentence: every run should produce a self-contained, redacted record that a harness can replay, a workspace delta that a check can assert against, and a tree of sessions rather than a flat list.

## What a transcript must contain

| Field group | Contents | Why it must be captured |
| --- | --- | --- |
| Identity | Session id, parent id and role, transcript schema version, start and end timestamps | A record with no stable identity cannot be selected, joined to its children, or cited by a corpus case. |
| Provenance | Kern build id, model name, pinned snapshot id, endpoint host, decoding parameters, `maxTurns`, `maxTokens` | Without it, two runs cannot be told apart, and a difference in score cannot be attributed to a change. |
| Inputs | System prompt text, tool definitions JSON, user prompt | These are the two variables evaluation exists to compare, and both must be byte-identical to what the model saw. |
| Per-turn I/O | Request payload, response message, assistant text, finish reason | The turn is the unit of failure, so a replay must be able to stop and restart at any of them. |
| Per-turn usage and timing | Prompt and completion tokens, wall time per request | Cost and latency regressions are first-class results and are otherwise unrecoverable after the fact. |
| Tool execution | Name, arguments, result, error code, duration | The tool call is where the agent's behaviour lives; a transcript without it records talk, not action. |
| Outcome | `Session::PromptResult` fields and a completion flag | The verdict of the run must live in the record, not in the caller that consumed it. |
| Workspace delta | Files read, files written, commands run, build status | This is what a check asserts against, and it cannot be reconstructed from message text. |

The provenance group is the one that decides whether a record describes a run or merely a conversation, and it must carry the Kern build identity, the model name and its pinned snapshot id, the endpoint host with no credential material, the decoding parameters actually sent, and both `maxTurns` and `maxTokens`. A record that omits any of these is a description of a run, not a run: it cannot be reproduced, only imitated. The endpoint host is included because a local and a remote deployment of the "same" model are not interchangeable inputs.

The division between the two groups is exact and must not be read either way round. The `inputs` group holds the system prompt, the tool definitions JSON and the user prompt — the text itself, which is what `SessionInputs` (`src/SessionRecorder.h:38`) carries — and nothing else. The `provenance` group holds the build identity, the model name, the endpoint with no credential material, the decoding parameters, `maxTurns` and `maxTokens`. Nothing in the manifest carries a hash, so a later change that adds one belongs to `inputs` and must not be read as provenance. That split is why [07 Evaluation harness](07-evaluation-harness.md) has to merge `inputs` and `provenance` when it populates a corpus case's provenance object.

The outcome group costs nothing new. `Session::PromptResult` already serializes itself through `operator nlohmann::json()` (`src/Session.cpp:949`), producing `ok`, `errorCode`, `content` and `turnsUsed`, so the recorder only has to place that object in the manifest.
The workspace delta group turns a transcript from a conversation into evidence, because it is the one group a check can be written against without parsing model prose. Files read and written are already visible to the tools through `ToolsRuntimeContext::fs` (`src/ToolUtilities.h:59`), and the commands run and the build status arrive as tool results, so the delta is derived rather than measured a second time. A delta that names paths is a delta a reviewer can later assert on.

Two omissions cut across the groups. The first is that nothing records what the recorder itself did — whether it wrote, warned, or was switched off — so the manifest's own presence is the only evidence of its success until that is added. The second is that timing is grouped with usage rather than kept separately, because a latency regression and a token regression arrive together far more often than either arrives alone.

Two cross-cutting decisions belong to this table. The request payload is stored once per turn rather than once per session, because a replayed turn must see the history as it stood at that turn and not as it ended. And every field a later reader might need to compare is stored as a value, never as a pointer to one, so a transcript remains readable after the process that wrote it has exited and the model name it references has been retired.

## The storage decision

The flat per-turn files stay. Several plans reference the `logs/<id>_<NNN>_*.json` naming, and continuity of the debugging artifact is worth more than a tidy scheme. Alongside them a run writes two artifacts per session, and only two, through `SessionRecorder` (`src/SessionRecorder.h:48`). The first is `<dir>/<id>_session.json`, a manifest whose keys are `schemaVersion`, `complete`, `identity{id, role, parentId, tag, sessionFile, turnsFile}`, `provenance{buildId, model, decoding, endpoint, maxTokens, maxTurns}`, `inputs{systemPrompt, toolDefinitions}` and `outcome`. The second is `<dir>/<id>_turns.jsonl`, one appended line per turn, carrying the turn number, a normalised `usage` with its `promptTokens`, `completionTokens` and `totalTokens` components, `responseMs`, `finishReason`, `assistantText`, and `toolCalls[]` and `toolResults[]` with string results cut to a 2000-character excerpt marked `resultTruncated`. Both are written through `RawWriteTextFile` (`src/FileUtilities.h:23`).

The split is driven by two properties rather than by taste. The manifest is written twice — `BeginSession` writes it with `"complete": false`, `EndSession` rewrites it with `"complete": true` — precisely so that a crashed run still leaves a readable identity: the start version carries everything known before the first token, the end version adds the outcome. The jsonl file is append-only, so a long session never re-serializes the whole history to add one turn, and a truncated final line is the only damage a crash can do.

The turn line is emitted without touching the call sites, which is why the coverage is complete rather than scattered. `Session::Pimpl` (`src/Session.cpp:54`) holds the recorder and a private nested `TurnRecord` guard (`src/Session.cpp:428`), constructed at `src/Session.cpp:546`, whose destructor emits exactly one turn line; `Session::Pimpl::Prompt` (`src/Session.cpp:912`) brackets the whole of `RunPrompt` (`src/Session.cpp:528`) with `BeginSession` and `EndSession`.

The structural work still pending is the workspace group. The manifest has no `workspace` key, so nothing in it describes the files a session read or wrote, the commands it ran, or whether the build passed — and that is precisely the group a check could be written against without parsing model prose. Files read and written are already visible to the tools through `ToolsRuntimeContext::fs` (`src/ToolUtilities.h:59`), and the commands run and the build status arrive as tool results, so the delta is derivable from the turn log rather than measured a second time. Two smaller requirements from the same table are owed with it: the manifest carries no start or end timestamp, and its provenance has no pinned snapshot id, so a run can be placed in a directory listing but not on a timeline, and a model name stands in for a snapshot that may already have moved.

## Recording is best effort, replay is not

A failed transcript write warns and continues. Recording is instrumentation, and instrumentation must never turn a working run into a failed one; a full disk or a read-only session directory is a reason to log one warning and disable further recording for that run, not to abandon a task that was making progress. `SessionRecorder::Enabled` (`src/SessionRecorder.h:83`) turns false after that warning, so the rest of the run is silent rather than a stream of identical messages, and nothing in the recorder throws. This is the opposite of the behaviour of `WriteLogFile` (`src/Session.cpp:80`), which throws, and it is the reason the manifest does not reuse it.

Completeness, however, is a different promise. A transcript that claims to be replayable must be complete, so the manifest carries `"complete": true|false`, and a transcript with `"complete": false` is refused by the harness rather than silently scored. A partial record is still useful evidence; it is simply not a case.

The redaction invariant holds for what the recorder can see. Credential material is never recorded, and the output of tools that touch secrets is recorded as-is only when it already passes the existing redaction path, which is the key-stripping at `src/NetworkUtilities.cpp:163-167`, where `ReplaceIn` rewrites the endpoint's API key to `[redacted]` before it is assigned to `HttpResult::body` (`src/NetworkUtilities.h:9`, the field itself). The recorder adds no second redaction mechanism, because two mechanisms drift apart. Path rewriting is still owed: no absolute path is rewritten relative to a session root, which is the reason a transcript's tool output is the bytes the tool produced, whatever directory it named.

The split between the two promises is what keeps the design honest. Best effort governs the act of writing, so a failure is reported and the task continues; completeness governs the claim the file makes about itself, so no reader has to guess whether silence meant "nothing happened" or "the writer could not reach the disk". A record that failed to write leaves the run unrecorded and the run's own success untouched, which is the correct ordering of concerns.

## Control surface

The surface is `--record on|off|auto`, `--session-dir` and `--session-tag`, plus the `showSessions` action, documented in `README.md` at the run options (`README.md:125`) and under `### Session transcripts` (`README.md:197`), parsed in `src/CommandLine.cpp` and carried to `Session::Config` through `CommandLineOptions` (`src/CommandLine.h:32`, fields at `:42-44`). `showSessions` is `src/main.cpp:198`, and it reads manifests only, printing the id, the tag, the model and the completion flag without touching the turn logs. The `enum class CommandLineAction` (`src/CommandLine.h:22`) is the small type that distinguishes it from an ordinary prompt run.

Subagent sessions must be recorded under the parent's session id with a role field, so a `research` or `implement` run appears as a child rather than as an unrelated top-level transcript. The subagent tool bundles are defined in `src/Tools/research.h` and `src/Tools/implement.h`, and sessions are created through `ToolsRuntimeContext::createNewSession` (`src/ToolUtilities.h:58`), a callable member assigned by the caller that constructs the context (`src/main.cpp:138`) and forwarded by those bundles, which is the single place the parent identity can be handed down without touching either tool: the lambda at `src/main.cpp:154` sets only `tag`, so `parentId` is written but never set.

The tag is the control that makes the corpus selectable. It lands in the manifest as a top-level `"tag"` string field, empty when unset, and is copied from there verbatim into every report, so a batch of runs labelled `tool-schema-v3` can be retrieved as a set after the fact without depending on file timestamps or on the directory layout having been stable. This `tag` is a free-form label on a whole recorded run, and it is deliberately distinct from the `tags` array of capability labels on a corpus case defined in [07 Evaluation harness](07-evaluation-harness.md).

## The injection seam

`Session::Pimpl::Prompt` (`src/Session.cpp:912`) reaches the transport through the file-local wrapper at `src/Session.cpp:32`, calling it at `src/Session.cpp:560` and `src/Session.cpp:639`. Neither replay nor a fake endpoint is possible while those calls are hard-wired, because there is no point at which a recorded response can be substituted for a live one.

What is owed is a member on `Session::Config` (`src/Session.h:30`) whose type is a new named alias declared exactly over the existing `HttpPost` signature — `HttpResult HttpPost(Endpoint const&, std::string_view payloadType, std::string_view payload, std::function<void(std::string_view)> const&)` (`src/NetworkUtilities.h:20`), with `HttpResult` (`src/NetworkUtilities.h:9`) as its result type — defaulted to `HttpPost` itself, so the call site needs no adapter and no wrapper. The alias is new code and does not exist yet. The shape follows the callable members already carried by `ToolsRuntimeContext`, and the two call sites in `Prompt` gain no branch, only an indirection.

This single seam is what makes recorded transcripts scoreable offline, and it must be added before any harness work. Admitting a transcript before the seam exists would bind the corpus to records that no replay can reproduce, because a record produced against a hard-wired transport cannot be guaranteed to have captured the same inputs a replay would supply.

Two properties of the seam matter for replay. It returns the whole `HttpResult`, including `status` and `error`, so a recorded transport failure can be replayed as a failure rather than being flattened into an empty body. And it is a value member of the configuration, not a global, so two sessions in one process can be driven by different transports, which is exactly what a harness scoring several cases concurrently requires.

What replay substitutes is a lookup: the case's transcript supplies the response recorded for a given turn, and the seam returns it without touching a socket. The substitute is a caller-side concern, and `Session.cpp` neither knows nor needs to know that its transport is recorded.

## From session to corpus case

Admission turns a recorded run into a versioned fixture, and it is a review, not a copy:

1. The recorder has already written a complete transcript, so no step here re-runs the model.
2. `kern_eval admit <session-id>`, from [07 Evaluation harness](07-evaluation-harness.md), copies the transcript into `eval/corpus/<case-id>/transcript.json`.
3. `case.json` is derived from the manifest: its provenance comes from the manifest's `provenance` and `inputs` groups rather than being re-typed.
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

The recorder has hermetic CTest coverage with no network access: `tests/SessionRecorderTests.cpp`, built as the `session_recorder_tests` executable and registered at `tests/CMakeLists.txt:100-110`, in the hand-rolled `Expect`/`ExpectEqual` style of `tests/TestUtilities.h`. It covers the manifest written at session start and again at session end, the append-only turn order together with usage normalisation across the OpenAI and Ollama spellings of the token counts, the disabling of recording after a failed write, and the absence of any temporary file left behind.

Two pieces of coverage remain pending. Path rewriting needs a test of its own once absolute paths are rewritten relative to a session root, which is the point at which there is something to assert. And a reader that refuses `"complete": false` and an unknown `SessionTranscriptSchemaVersion` (`src/SessionRecorder.h:15`) arrives with the harness in [07 Evaluation harness](07-evaluation-harness.md), because refusing a record is a decision only the consumer makes.

One constraint shapes the file layout. `src/Session.cpp` is not compiled into any test executable, and compiling it would drag in the HTTP client and the whole tool runtime, so the manifest writer lives in its own small translation unit — `src/SessionRecorder.cpp`, listed at `CMakeLists.txt:47` — that a test compiles alone.

Registration follows the existing pattern exactly. The translation unit joins the core block of the `kern` target in `CMakeLists.txt:31-59`, and the test joins `tests/CMakeLists.txt` next to the eight executables and their `add_test` calls already registered there (`tests/CMakeLists.txt:7-98`), with its own source list, `target_include_directories`, `target_link_libraries` and `add_test`.

## Failure modes

- A crash mid-session leaves `"complete": false` and an unanswered request in the last jsonl line, which the harness must treat as unscoreable rather than as a low score.
- A tag drifts into meaninglessness once nobody agrees what `v3` denotes, so the manifest's top-level `"tag"` string field is copied verbatim into every report alongside the provenance block.
- A transcript recorded without the seam is evidence only, and the harness must refuse it rather than score it, because `SessionTranscriptSchemaVersion` (`src/SessionRecorder.h:15`) cannot tell a replayable record from an un-replayable one.
- Recording on a machine without a writable `--session-dir` must degrade to a warning and an unrecorded run, never to a failed task.
- A transcript recorded against a different Kern build is replayable for scoring, but its build id must be surfaced in every comparison report so that a cross-build difference is never read as a model difference.

## Open questions

1. Whether `logs/` and the session transcript should eventually be one directory: the per-turn files are debugging aids that expire with the build, the transcript is a record that outlives it. Leaning toward keeping them separate and having `--session-dir` default outside the build output.
2. Whether the recorder should hash the workspace tree at start: a tree hash gives cheap deduplication and detects a stale fixture, but hashing a large repository costs more than the record it guards. Prefers hashing only the files the session later touches.
3. Whether subagent transcripts should be nested inline in the parent's jsonl or kept as separate directories: inline keeps one file per session tree, separate directories keep each replay self-contained. Leaning toward separate directories with a parent id field, because replay granularity should match scoring granularity.
4. Whether the recorder should capture the full request payload per turn when the per-turn file already does: duplication is safe and removal is not. Deferred, pending a measurement of transcript size on a long session.
