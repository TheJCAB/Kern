# Evaluation harness design

Status: proposal, nothing implemented. The CTest suite in `tests/` verifies units — string helpers, file utilities, path validation, session recording — and this plan adds a separate system that scores behaviour against recorded transcripts. It depends on [06 Session recording](06-session-recording.md): the recorder writes the manifests and turn logs this harness reads, but until its `sendRequest` seam lands those records cannot be replayed, so there is nothing to score yet.

Related designs: [06 Session recording](06-session-recording.md) (the manifests, turn logs, proposed transport seam and `admit` command this harness consumes), [08 Evaluation programme](08-evaluation-programme.md) (which decides what the corpus must be able to discriminate), and [09 Turn budget and session resumption](09-turn-budget-and-resumption.md) (whose progression and resumption behaviour is one of the things a case may need to pin); each of the three links back to this plan.

## Why this is not CTest

CTest runs inside the build. `enable_testing()` in `CMakeLists.txt:9` and `add_subdirectory(tests)` in `CMakeLists.txt:78` register executables that the same `cmake --build` produces as `kern`, that must be hermetic with respect to the network and to every path outside the build tree, that must be deterministic, and that must finish in seconds. Each one is registered per executable in `tests/CMakeLists.txt` — `add_test(NAME string_utils_tests COMMAND string_utils_tests)` at `tests/CMakeLists.txt:15` is the pattern — and asserts through the hand-rolled `Expect`/`ExpectEqual` helpers in `tests/TestUtilities.h`, which throw on mismatch and terminate non-zero.

Evaluation is the opposite on every axis. It calls a live model over the network, so it is neither hermetic nor credential-free. It costs money on every run. It is statistical: the same case on the same model returns different tool arguments across trials, so one result is not a verdict. It is slow: a session-level case runs tens of turns and a useful suite runs for hours. And it produces a report — pass rates, intervals, deltas, token means — rather than a pass or a fail.

| Property | CTest suite in `tests/` | Evaluation harness |
| --- | --- | --- |
| Transport | None; pure in-process calls. | HTTPS to a live endpoint, or a recorded substitute. |
| Cost | Zero. | Tokens billed per trial. |
| Determinism | Required; a flaky test is a bug. | Impossible; variance is the subject being measured. |
| Runtime | Seconds. | Minutes per case, hours per suite. |
| Output | Exit code, pass/fail per test. | A report with per-case rates and intervals. |
| Trigger | Every build. | Requested by a person, or by a CI job outside the build. |

The two systems do share one thing worth naming: a failure must be attributable. A CTest failure names an executable and a source assertion; an evaluation failure names a case, a trial and a check. Neither is allowed to report only a number.

The consequence is a hard rule: no evaluation case may ever be registered as a CTest test, and nothing under `eval/` may be read by the build. A case that reached CTest would make every test run cost money, make the suite flaky by construction, and bind a versioned review artifact to a build graph that has to stay hermetic.

The narrower rule sits beside it and does not contradict it. The harness's own internals — loading a case, replaying a transcript, evaluating a check, formatting a report — are ordinary deterministic code and are unit-tested by CTest, hermetically, in the style of the existing tests. A harness nobody tests silently scores everything green, and a gate that always passes is worse than no gate.

## Two levels of evaluation

| Level | What is held fixed | What varies | Scoring | Typical use |
| --- | --- | --- | --- | --- |
| Prompt case | Everything: transcript prefix, tool list, model, decoding. | Exactly one thing: system prompt text, one tool description, one tool schema, the tool list, or the model name. | One turn, comparing the chosen tool call and its arguments against expectations. | Prompt and schema iteration on every commit. |
| Session case | Task prompt, workspace fixture, model, budget. | Nothing but the change under test. | Trajectory and outcome over the whole agent loop. | Confirming that a prompt-level gain is a real gain. |

A prompt case is a single request against a fixed recorded transcript prefix. The prefix is taken from a committed `transcript.json`, the tool definitions are rebuilt, and the request is sent once. Exactly one dimension is varied, and which one is recorded in `case.json` under `variance`. Scoring is a comparison of the model's chosen tool call and arguments with the case's expectations: the right tool, the right argument values, no tool at all when none was due. Prompt cases are cheap, fast, and deterministic enough to run on every commit. They also cannot detect any multi-turn effect, because there is no second turn.

A session case runs the whole agent loop from a fixed task prompt and a workspace fixture. Scoring is on trajectory and outcome: which tools were called in what order, what the workspace contained afterwards, whether the build passed, whether the answer contained the required statement, and how many turns and tokens it took. Session cases are expensive, noisy and definitive — definitive because they are the only level at which the goal itself is measured.

Prompt cases exist to make prompt and schema iteration fast, and they can be gamed. An agent can score well one step at a time and still fail the task: a tool description can be tuned until the first call is always correct while the run still loses the thread at turn twelve. The corpus therefore has to contain enough session cases for that confirmation to mean something, and every prompt-level improvement must be confirmed by a session-level pair — the same cases, the same repetitions, before and after.

Variances compound, which is why a prompt case records the exact tool-definitions payload it was recorded with. Changing one tool's JSON schema changes the tool list, which changes the prompt prefix for every other case in the corpus, so a score that moves after a schema edit may have moved for a reason nobody intended. The manifest keeps that payload verbatim in its `inputs` group rather than as a digest, so the harness computes the digest when it loads a transcript and writes it into `case.json` as `provenance.toolsSha256`; `case.json`'s `provenance` object merges the manifest's `provenance` group with that digest and the prompt digest, so the key name here describes the case's grouping, not the manifest's. A mismatch between the case's recorded digest and the digest being evaluated is reported rather than hidden.

The cost asymmetry between the levels is what makes the two-level design work rather than merely sound. A prompt case is one request: a handful of seconds and a few thousand prompt tokens, so a corpus of a hundred of them costs less than a single session case. A session case is tens of requests over a cloned workspace, so it is affordable only when it answers a question the prompt cases cannot. The corpus is therefore shaped like a pyramid, wide at the prompt level and narrow at the session level, with the narrow top carrying the decisions.

## Corpus layout

```text
eval/
  corpus/<case-id>/     case.json  checks.json  transcript.json  fixture/  notes.md
  suites/               gate.json  <suite>.json
  trials/<experiment-id>/
  reports/
```

`eval/corpus` and `eval/suites` are version controlled: they are reviewed claims about intended behaviour, and a change to either is a change to what the project promises. `eval/trials` and `eval/reports` are generated on every run, grow without bound, and contain model output; they are gitignored. Every path in a case is relative to the repository root or to the case directory, never absolute.

| Key | Meaning |
| --- | --- |
| `id` | The case identifier, which is also its directory name under `eval/corpus`. |
| `level` | `prompt` or `session`; selects the runner and the legal check kinds. |
| `tags` | Capability labels such as `edit`, `build-recovery`, `tool-choice`; suites and reports select by tag. |
| `reviewed` | Whether a human has confirmed that the case asserts intended behaviour. |
| `reviewed_by`, `reviewed_at` | Who reviewed it and when, so an unowned case is visible. |
| `provenance` | Populated from the transcript manifest's `provenance` group plus the prompt sha256 and tools sha256 the harness computes from the text and JSON the manifest keeps in its `inputs` group; the manifest itself carries no hash. Fields are the build id, model, pinned snapshot, prompt sha256, tools sha256. |
| `variance` | The single dimension varied for a prompt case: system prompt, tool description, tool schema, tool list, or model name. |
| `expect` | The per-case summary of intent, restated for a reader; the scored assertions live in `checks.json`. |
| `budget` | Maximum turns, maximum tokens, and a wall-clock ceiling after which the trial is abandoned. |

A suite file in `eval/suites` names case ids or tags, a repetition count, a mode, and the gate settings it applies. The suite is the only place a decision about coverage lives, so adding a capability to the gate is a one-line edit to a reviewed file rather than a change to the runner.

`reviewed` is the field the gate turns on. An unreviewed case is skipped by every gating suite and is reported separately as unreviewed, because a case nobody has read is a case whose pass may mean nothing. Review is what converts an observation into an assertion, and `reviewed_by` keeps the assertion owned.

## Checks

Scoring is a list of named assertions from `checks.json`, evaluated against the transcript the trial produced. Each yields pass, fail, or not-applicable when the check kind does not apply to a transcript of that level. A case fails if any check fails.

| Kind | Asserts | Applies to | Example |
| --- | --- | --- | --- |
| `expect_tool` | The tool chosen for the turn. | Prompt and session | `{ "kind": "expect_tool", "tool": "edit_file_lines" }` |
| `expect_args` | Argument values, by partial JSON matching, so unspecified keys are ignored. | Prompt and session | `{ "kind": "expect_args", "tool": "read_file", "args": { "path": "src/main.cpp" } }` |
| `expect_sequence` | Order of tool calls, with `allow_extra` tolerating interleaved calls. | Session | `{ "kind": "expect_sequence", "tools": ["read_file", "edit_file_lines", "build"], "allow_extra": true }` |
| `expect_file_state` | File content, absence, or a line-count bound after the run. | Session | `{ "kind": "expect_file_state", "path": "src/Session.cpp", "lineCountAtMost": 12 }` |
| `expect_build_ok`, `expect_command_exit` | That the build succeeded, or that a command exited with a given code. | Session | `{ "kind": "expect_build_ok" }` |
| `expect_answer_contains`, `expect_answer_matches` | Substring or regular expression in the final answer. | Prompt and session | `{ "kind": "expect_answer_matches", "pattern": "src/Session\\.cpp:\\d+" }` |
| `expect_turns_at_most`, `expect_tokens_at_most` | That the run stayed inside its budget. | Session | `{ "kind": "expect_turns_at_most", "turns": 20 }` |
| `expect_no_repeated_failure` | That no identical failing call repeats beyond a threshold, catching loops. | Session | `{ "kind": "expect_no_repeated_failure", "repeats": 2 }` |

Checks are declarative JSON, not code. A case that needs arbitrary code to score is a case whose desired behaviour is not understood well enough to test, and free-form judgement belongs in human review, not in the gate. This is also what keeps the corpus reviewable: a reviewer reads eight assertions and can say whether they describe the intended outcome.

Checks read only the transcript and the recorded workspace delta, never live repository state. That is the same boundary [06 Session recording](06-session-recording.md) draws around `"complete": true`, and a transcript refused as incomplete is refused before scoring rather than after, so a case never fails for a reason the harness could have reported as unscoreable.

A case is pass or fail for gating, but the report keeps every per-check result. A regression therefore shows which assertion broke — the build stopped passing, the third tool call disappeared, the answer stopped citing a path — rather than merely that a case turned red. That granularity is the difference between a report someone acts on and a number someone argues with.

## Comparing experiments

An experiment is a set of case ids, a model configuration, a repetition count, and the code and prompt revisions under test. It is identified by an id under `eval/trials/<experiment-id>/`, and every trial records the provenance block from [06 Session recording](06-session-recording.md) so the experiment is reconstructible after the model has been retired.

Evaluation is stochastic, so a single run proves nothing. Each case runs N times, defaulting to three and configurable per suite, and the report gives a pass rate per case with an interval rather than a scalar. Three trials is enough to distinguish always from never and not much else; that is accepted deliberately, because the alternative is a suite nobody can afford to run.

The interval is a Wilson score interval over the trial results, which stays usable at three or five trials where a normal approximation would not. A case at three of three and a case at two of three therefore do not merely differ in a fraction; the report can state how much evidence each actually carries.

Comparison is paired. The same cases, the same repetitions, the same ordering of runs, and exactly one varied dimension, so the report states a delta and a count of cases improved, regressed and unchanged. Comparing two unpaired batches across a shared repository state cannot attribute a difference to anything.

Reporting rules follow from that. A global aggregate score is forbidden as a decision input, because averaging hides regressions in individual capabilities — a suite can improve by two percent while every file-editing case has started failing. Decisions are made on per-capability and per-case deltas, with aggregate numbers shown only as context, and the gate reads individual cases, never the aggregate.

| Case id | Level | Trials passed | Pass rate | Mean turns | Mean tokens | Wall clock | Delta vs baseline | Broken checks |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `edit-preserve-eol` | session | 3/3 | 1.00 | 9.3 | 41k | 71s | 0 | — |
| `schema-read-chunk` | prompt | 2/3 | 0.67 | 1.0 | 6k | 3s | -1 | `expect_args` |

Cost and latency are captured per trial from the recorded usage, because a change that improves quality two percent for triple the tokens is usually a loss, and the report has to make that visible in the same table as the pass rate.

## The runner

The harness is a separate executable, `kern_eval`, added as its own target in `CMakeLists.txt` beside `kern`. It links the same core translation units — `src/Session.cpp`, `src/ToolUtilities.cpp`, the tool headers — because it drives the real agent. It is never linked into `kern` and is never modelled by an agent tool: an evaluation harness the agent can call is a harness the agent can optimize against.

Three execution modes decide what the seam answers.

| Mode | Transport | Purpose |
| --- | --- | --- |
| `live` | The real endpoint and model named by the case. | Producing new evidence. |
| `replay` | Responses read from the case's `transcript.json`. | Unit-testing the harness itself. |
| `mock` | A scripted fake endpoint supplied through the transport member that [06 Session recording](06-session-recording.md) proposes adding to `struct Config` (`src/Session.h:30`); that member does not exist yet. | CTest runs of the harness internals. |

`live` mode refuses to start unless the resolved service names a model and the environment holds its key, so a misconfigured experiment fails before it spends anything. `replay` and `mock` never resolve a service at all, which is what lets the harness's own tests run on a machine with no credentials and no network.

The commands form a small surface, and each one is also reachable from the unit tests through the same entry points.

| Command | Effect |
| --- | --- |
| `run` | Executes a suite or a case list in a given mode and writes a trial directory. |
| `compare` | Pairs two trials or an experiment against a baseline and emits the delta table. |
| `admit` | Turns a recorded session into a case directory, per [06 Session recording](06-session-recording.md). |
| `list` | Prints cases with their level, tags, review state and provenance. |
| `report` | Renders a trial directory as a report, including per-check detail. |
| `prune` | Deletes trial and report directories older than a stated age. |

`run` isolates each case. Every case gets a fresh temporary workspace cloned from its `fixture/`, the fixed `Kern.build.json` the fixture carries, a per-case turn and token budget enforced during the run, and a teardown that records the file delta before the workspace is removed. Nothing a case does is visible to the next case, and nothing it writes survives the run except the transcript and the recorded delta.

The harness invokes the agent through the same `Session` code path the product uses rather than reimplementing the loop, because a harness with its own loop measures a different program. This is the reason for the injection seam in [06 Session recording](06-session-recording.md): the only substitution is transport, and the loop, tool dispatch, history handling and budget checks are the shipped ones.

Cases may run in parallel with a bounded worker count, since a session-level suite is otherwise dominated by waiting on the model. Every case must therefore be idempotent and must never write outside its own workspace, and no case may read a path another case is writing.

The harness's own scoring and report code is unit-tested hermetically, following the pattern set for the recorder in [06 Session recording](06-session-recording.md): a new executable in `tests/CMakeLists.txt` compiles the loading, checking and formatting translation units with no network client linked, driven entirely by `mock`.

## Regression gates

`eval/suites/gate.json` lists the reviewed session cases that must pass. `kern_eval run --suite gate` returns a non-zero exit code when any gate case regresses, and it is what a maintainer runs before merging a prompt, schema or tool-list change — manually, or from a CI job, never from the build.

The gate is deliberately narrow. It contains reviewed session cases only, because prompt cases are for iteration and a gate built on them would reward exactly the local optimisation this document warns against. It runs the same repetition count as the experiment it judges, so a gate result and an experiment result are comparable.

The boundary is human. The gate reports, a person decides, because a threshold can be satisfied by a worse agent: a change that makes every gate case pass by making the agent more conservative, or by shortening its trajectories at the cost of correctness, satisfies the gate and degrades the product. The gate exists to make regressions loud, not to make decisions.

## Acceptance criteria

1. A replayed transcript scores identically to the original live run of the same case.
2. A prompt case with a recorded variance set to none scores the same as its source transcript until the variance is applied.
3. A deliberately broken tool description produces a measurable drop on the prompt cases that touch that tool.
4. Deleting one check from a case changes only that case's result and no other.
5. Two trials of the same case on the same model agree often enough that the stated interval is meaningful.
6. No evaluation path writes into the repository working tree or into `build/`.

## Open questions

1. Whether the harness reuses the existing services configuration or carries its own: `struct ServiceDefinition` (`src/Services.h:14`) already holds endpoint, key variable, model and token limit, and `ResolveService` (`src/Services.h:44`) already validates them and resolves the key, so a second format would duplicate the validation and drift from it. Leaning toward reusing `LoadServices` with a harness-side overlay for repetition count and budget.
2. Whether cases should pin a model snapshot rather than a model name: a name resolves to whatever the provider currently serves, which makes an old report unreproducible, but a pinned snapshot is unavailable for some endpoints. Leaning toward pinning where the provider exposes a snapshot and recording the name otherwise, with the difference surfaced in every report.
3. How to score the `research` and `implement` subagents as trees rather than single trajectories: their sessions are nested under a parent id, so a check could address a role-scoped subtree, which no check kind currently expresses. Deferred, pending a corpus that actually contains subagent cases.
4. Whether checks should support a `weight` for partial credit: weighted checks would let a case express "the build must pass, the style is secondary", at the cost of a threshold that can be met by a worse run. Leaning against, keeping per-check results for diagnosis and pass/fail for the gate.
5. Whether the harness should support a smoke mode that runs entirely without network: `mock` already does this for the internals, but a smoke mode over the real corpus would let CI verify that every case loads and every check parses without a single token being billed. Leaning toward yes, as a CTest test rather than a `kern_eval` mode.
6. Whether `replay` should be able to re-score every historical trial after a check is edited, so that a fixed assertion is applied retroactively. Leaning toward yes, because it costs nothing and turns old trials into a backfill for new checks.
