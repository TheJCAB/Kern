# Fix: src/Tools/subagent.h

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`, `24-tool-utilities.md`, `22-session-recorder.md`; if any has not landed, stop and say so.
Goal: make the system-prompt read injectable and stop the shared subagent runner from hiding an unqualified constant.

## What is wrong

1. The runner reads its system prompt from a file under the runtime data directory with no injection point, so prompt assembly cannot be tested without a real data directory of the right shape.
2. The turn constants are referenced unqualified and are not declared in this header, so the unit's dependency on another unit is invisible from this file.
3. It has no test: the existing subagent test targets the session-registry unit, so the one place where session creation and release are actually called is unexercised.

## What to change

1. Accept the system prompt as a string or as an injected provider defaulting to the current file read, and document that default and the file it reads.
2. Include or forward-declare the constant it uses, so the file's dependencies are honest and a reader can see where the bounds come from.
3. Document the runner contract above the declaration: the argument bounds on turns, the empty-prompt refusal, the mapping from a stopped-at-turn-limit outcome to the reported status, and the release-on-refusal rules, stating exactly how many times release is called on each path.
4. Give it a test target that injects the session factory, the release function and the prompt, with no model, thread, clock or filesystem.

## Non-goals

- Do not change the subagent session registry, which the review names as the repo's model unit.
- Do not change turn limits.
- Do not change the shared envelope.
- Do not change the two tools that call this runner beyond matching the new signature, which prompt 32 owns.

## Acceptance

- The declared build command succeeds.
- A new `subagent_runner_tests` target exists and compiles only this unit, the shared envelope's unit and the test utilities.
- With all three dependencies injected, it asserts: a normal run returns the created session id and its final text; an empty prompt is refused with the named code and creates no session; a turns argument below or above the documented bounds is refused with the named code; a session that stops at its turn limit reports the turn-limit status rather than complete; a post-creation refusal releases the session exactly once, asserted through the injected callback's call count; the prompt provider is called once per run.
- No network, no threads, no real files; the test file says so at the top.
