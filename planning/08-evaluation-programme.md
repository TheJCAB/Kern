# Evaluation programme

Status: in progress. The transport seam, the workspace delta, `kern_eval`, the corpus and the gate are what remain, in the order of work below. This document coordinates [06 Session recording](06-session-recording.md) and [07 Evaluation harness](07-evaluation-harness.md), and owns the policy neither of them can decide.

Related designs: [06 Session recording](06-session-recording.md) (the redacted manifests, jsonl turns, recorded provenance and `sendRequest` seam that turn a run into a replayable record), [07 Evaluation harness](07-evaluation-harness.md) (the `kern_eval` runner, declarative checks, paired comparison and gate modes that score a record repeatedly), and [09 Turn budget and session resumption](09-turn-budget-and-resumption.md) (the budget and continuation policy a long run depends on). Each of the three links back to this programme.

## The problem

Kern's behaviour is configured today by artifacts that are edited by intuition. The system prompts in `data/CommonSystemPrompt.txt`, `data/MainSystemPrompt.txt`, `data/ResearchSystemPrompt.txt` and `data/ImplementSystemPrompt.txt` decide what every agent is told; the tool descriptions and parameter schemas are declared as `constexpr ToolDefinition` arrays (`struct ToolDefinition` at `src/ToolUtilities.h:89`) across the headers in `src/Tools/`; the set of tools offered to the main agent is `constexpr ToolDefinition MainTools[]` (`src/main.cpp:44`) while the subagent bundles are the child arrays `constexpr ToolDefinition Tools[]` in `src/Tools/research.h:21` and `src/Tools/implement.h:25`; and the choice of model, endpoint and token limit is the `struct ServiceDefinition` (`src/Services.h:14`) the user names. The unit tests in `tests/` prove that those artifacts are well formed — that a schema parses, that a description is non-empty, that a header compiles — and they prove nothing whatsoever about whether an agent holding them behaves well.

Every edit to that set is therefore an unmeasured change. Nobody can say whether a wording change in `data/CommonSystemPrompt.txt` helped, whether the tool description it replaced was carrying weight, or whether swapping the model in the same change destroyed the gain. The usual failure mode is not a regression that announces itself with a failing test or a crash; it is a slow drift in which the agent becomes slightly worse at a capability nobody is watching, and the drift is discovered months later by a user who cannot tell which of forty changes did it. The goal of this programme is the discipline that closes that loop: a conservative, version-controlled corpus of reviewed cases, a harness that scores against it, and a rule that a prompt, schema, tool-list or model change is accepted only with evidence that it did not make the agent worse and, if it claims to make it better, evidence for that too. Recording turns a run into a record; what the programme still lacks is the seam that makes a record replayable, the harness that scores it, and the corpus that decides what a score means.

## The two proposals

| Document | What it does | Cost | When it runs |
| --- | --- | --- | --- |
| [06 Session recording](06-session-recording.md) — manifests, jsonl turns, recorded provenance, `sendRequest` seam | Writes one self-contained, redacted, replayable transcript per session: identity, provenance, prompt and tool definitions, per-turn I/O, usage and tool execution, with the injection seam in `Session::Config` and the workspace delta still to come | Disk for the transcripts and a negligible append per turn; no model calls of its own | On every recorded session, by default and without a flag |
| [07 Evaluation harness](07-evaluation-harness.md) — `kern_eval`, prompt and session cases, declarative checks, paired comparison, gate modes | Scores a corpus case in `live`, `replay` or `mock` mode and reports per-case pass rates, intervals, turns, tokens and deltas | Real model calls and wall clock, in proportion to the suite asked for | On demand, and in the pre-merge gate; never in the build |
The division of ownership is what keeps the two documents from describing each other. Recording owns what a run leaves behind and nothing about scoring; the harness owns what a case asserts and nothing about capture. The programme owns the part neither can: when a change must be evaluated, which cases are allowed to block a merge, who is required to have read them, and what counts as evidence when the answer is contested. Every one of those is a policy about people rather than a mechanism inside a binary, which is why it belongs in a document of its own rather than as a section of either design.

## How they fit together

- A recorded session → **evidence**: the transcript exists so that a run can become a case without anyone reconstructing what happened from memory or from console output.
- A corpus case → **comparison**: the harness exists so that one case can be scored repeatedly, against a varied dimension, and two batches can be paired into a delta.
- A paired delta → **verdict**: the programme exists so that the delta is attached to a change and a person has to look at it before the change is merged.
- `eval/suites/gate.json` → **blocking**: the suite exists so that a reviewed case, once selected, stops a merge instead of merely informing one.

Neither half is useful alone. A harness without transcripts measures nothing real, because a case invented in the abstract is a case whose expectations nobody has watched a model fail; transcripts without a harness measure nothing at all, because a directory of records nobody scores is a museum. The `sendRequest` seam in [06 Session recording](06-session-recording.md) is the single shared mechanism that makes both cheap: it is the only point where transport is substituted, so replay is one indirection and the agent loop under test remains the shipped one. It lands before any transcript is admitted, because a session recorded against a hard-wired transport cannot be replayed and a corpus cut from such records would have to be discarded rather than migrated. The decisions in [06 Session recording](06-session-recording.md) also define the manifest's field groups — `provenance`, `inputs`, per-turn usage and outcome — and the harness in [07 Evaluation harness](07-evaluation-harness.md) reads only those groups, so neither document redefines a field name the other owns.

## The corpus lifecycle

A case moves through five states, and each transition has a rule rather than a habit.

1. **Captured** — a complete transcript exists. The manifest says `"complete": true`, so the record is whole; an incomplete one stays in the transcript directory as evidence and is never admitted.
2. **Admitted** — `kern_eval admit` copies the transcript into `eval/corpus/<case-id>/` and the case lands with `reviewed` set false. Admission is a beginning of review, not an end of it.
3. **Reviewed** — a human has read the case, set `reviewed_by` and `reviewed_at`, trimmed or pinned the transcript so the case asserts what it claims to, and confirmed that the checks describe intended behaviour rather than accidental behaviour.
4. **Gating** — the case id is listed in `eval/suites/gate.json`, so a regression on it blocks a merge. Only a reviewed case may reach this state, and the suite file is the only place coverage is decided.
5. **Retired** — the case is removed, and the change that removes it records why: superseded by a better case, no longer describing intended behaviour, or no longer reproducible.

The discipline that keeps those states meaningful is small and strict. A case may not enter a gate suite while it is unreviewed, because an unread case that passes is indistinguishable from one that asserts nothing. A case may not be edited to make a change pass, except in the same change that explains why the old expectation was wrong, so a weakened assertion is always visible beside the change it protects. Every retired case leaves a note, so a capability that loses its coverage says so. The corpus is reviewed in exactly the way source is reviewed, on a diff, by a person, because a case is a claim about intended behaviour and claims are the thing review exists for. And cases are added when a capability is deliberately introduced, not only when something breaks, since a corpus grown only from failures covers only the failures that already happened.

A case carries a level, a capability tag and a provenance block, and the tag is what makes a delta attributable. A report grouped by tag answers the question a maintainer actually asks — did file editing get worse — where a report grouped by case answers only whether one assertion still holds. Tags are therefore part of review: a case admitted without one is a case that will be scored and never consulted, and a case claiming two capabilities is a case whose regression will be blamed on the wrong change.

## What is measured, and what is never a metric

Decisions are made on per-capability and per-case deltas for paired experiments. The report carries a pass rate per case with an interval over repeated trials, plus mean turns, mean tokens and wall clock, and the comparison keeps cases improved, regressed and unchanged as separate counts. A single global score is forbidden as a decision input, because an average lets a regression in one capability hide behind an improvement in another: a suite can improve by two percent while every file-editing case has started failing, and the aggregate will call that a win.

The classes of change that must be evaluated before acceptance are:

- System prompt edits, in `data/CommonSystemPrompt.txt` and in the three role prompts, which reach every agent in the tree.
- Tool description and parameter schema edits, wherever a `constexpr ToolDefinition` declares them.
- Adding or removing a tool, in `constexpr ToolDefinition MainTools[]` (`src/main.cpp:44`) or in the child bundles `constexpr ToolDefinition Tools[]` of `src/Tools/research.h:21` and `src/Tools/implement.h:25`.
- Changing the model or its pinned snapshot in the `struct ServiceDefinition` (`src/Services.h:14`) a run resolves.
- Changes to memory or context management of the kind proposed in [05 Memory and context programme](05-memory-and-context-programme.md).

That last class is the reason this programme is written before those four plans are implemented. [01 Context compaction](01-context-compaction.md), [02 Memory tool](02-memory-tool.md), [03 Session reset](03-session-reset.md) and [04 Shared note store](04-shared-note-store.md) all claim to make long runs better, and each of them changes what the model sees on every turn; without a harness and a corpus, none of the four can be shown to help, and the likeliest outcome is four plausible mechanisms merged on argument alone.

## Review burden and cost

This programme fails in two ways, and both are cheap to predict. It becomes expensive enough that nobody runs it, so the gate is skipped under deadline pressure and the corpus becomes decoration; or the corpus grows unreviewed, so the numbers are computed from cases nobody has read and mean nothing. Either failure ends in the same place, which is a project that believes it is measuring behaviour while it is not.

The mitigations are deliberate and mostly about saying no. The gate suite stays small — a target of roughly a dozen session cases, each covering a distinct capability — while the full corpus may be large, because breadth belongs in evidence and only a narrow set belongs in a merge blocker. Prompt cases carry most of the iteration load, since one is a single request and a hundred of them cost less than one session case. Repetition counts default low and rise only for a decision that matters, so the cheap question is asked cheaply and the expensive question is asked properly. And the review step is bounded by requiring a case only when a capability is at stake, which means a wording tidy-up needs no new case while a new tool needs one. The same bound applies to admission: every recorded transcript is a candidate, and only a small minority should survive it, because a corpus of everything discriminates nothing.

## Common implementation notes

- New source and header files are added explicitly to the source list in `CMakeLists.txt:31-59`, in the core block or the `src/Tools/*.h` block, because the build uses no globbing and an unlisted file is silently not compiled.
- `kern_eval` is a second executable target beside `kern` in the same `CMakeLists.txt`, sharing the core translation units and never linked into the product binary.
- The harness's own internals are tested hermetically and registered as ordinary CTest tests with `add_test` in `tests/CMakeLists.txt`, while no evaluation case is ever registered there.
- Prompt text lives in `data/` and is copied next to the binary by the `copy_runtime_data` target, so a prompt case reads the same files the product does rather than a copy taken at build time.
- Everything generated goes under `eval/trials/<experiment-id>/` and `eval/reports/`, which are gitignored, and never under `build/`; `eval/corpus` and `eval/suites` are version controlled.
- The harness is never exposed as an agent tool, neither in `constexpr ToolDefinition MainTools[]` (`src/main.cpp:44`) nor in a subagent bundle, because a harness the agent can call is a harness the agent can optimise against.
- A recording whose manifest says `"complete": true` is the only input the harness accepts, and it stays evidence until it has been admitted and reviewed, so capture and scoring are joined by one rule rather than by convention.

## Suggested order of work

1. Add the `sendRequest` seam and prove that a session recorded through it replays byte-identically, before any transcript is admitted. The call site in `Session::Pimpl::Prompt` (`src/Session.cpp:912`) still reaches the transport directly, at `src/Session.cpp:560` and `src/Session.cpp:639`, so no session the current code records can be replayed.
2. Record the workspace delta and rewrite absolute paths relative to the session root, so that a transcript carries the effects a check asserts against instead of only the text of a tool's result.
3. Nest a subagent session under its parent's session id, so that a `research` or `implement` run appears as a child role rather than as an unrelated top-level transcript.
4. Build `kern_eval` with `replay` and `mock` modes only, and unit-test it hermetically, so the harness is trustworthy before it costs money.
5. Admit a first small corpus by hand from the transcripts already collected, and review every case in it.
6. Turn on `live` prompt cases, which are cheap and carry the iteration load from then on.
7. Add session cases and the pre-merge gate once the corpus contains cases worth blocking a merge on.

Steps 1 to 3 complete the capture side of [06 Session recording](06-session-recording.md); 06's `kern_eval admit` admission flow lands with step 5 and the harness's own hermetic tests with step 4. Nothing before step 7 changes how the agent behaves: the first six steps make behaviour observable and scoreable, and only the last one makes it blocking.

## Risks and open questions

1. Overfitting to the corpus: a reviewed case is a fixed expectation, and prompts tuned against it will eventually satisfy it without satisfying the task. The paired-comparison rule and the requirement that every prompt-level gain be confirmed at session level mitigate this but do not remove it, since both sides of a pair can be overfitted together. Leaning on the session level as the final authority.
2. Corpus rot as the product changes: paths move, tools are renamed, fixtures stop building, and cases fail for reasons unrelated to the change under test. Leaning toward treating a case that cannot run as a defect with an owner, reported as unreviewed rather than skipped silently.
3. The temptation to game prompt cases one step at a time, since each individual case is cheap and immediately satisfiable. Leaning toward requiring the confirming session pair before a prompt edit is called a gain.
4. Whether recordings belong in the repository at all, given that a corpus of transcripts grows without bound and mixes reviewed claims with incidental evidence. Leaning toward version-controlled cases and a separate store for the raw captures they were cut from.
5. The absence of any statistical expertise in the current tooling: a Wilson interval over three trials is honest about how little it knows, but the choice of repetition count and the reading of overlapping intervals are judgement, not computation. Deferred until a decision actually turns on a difference the interval cannot separate.
6. Whether the programme should own a canonical list of capabilities to hang cases on, so that coverage is a checklist rather than an accident of which failures hurt. Leaning toward yes, as a table in this document maintained beside `eval/suites/gate.json`.
