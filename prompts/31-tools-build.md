# Fix: src/Tools/build.h

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`, `11-run-process.md`, `24-tool-utilities.md`; if any has not landed, stop and say so.
Goal: give the best-documented tool in the repo its missing success-key table, and make its process spawn substitutable through the seam prompt 11 created.

## What is wrong

1. It is the only tool with a named error-code vocabulary, but the success keys appear only where they are emitted, and no header table lists them, so a caller cannot know the full result shape without reading the whole body.
2. It calls the process runner as a free function, so command selection and output truncation cannot be tested without really spawning a shell.
3. The build-command availability rule is described in a comment shared with a sibling tool but enforced only by the caller deciding whether to append the tool, so the comment and the enforcement live in different files.

## What to change

1. Document the full result contract above the tool as a short table: every success key with its type and meaning, and every named error code with the condition that produces it. Keep the table beside the existing prose rather than replacing it.
2. Accept the process runner as an injected callable, defaulting to the real runner, following the seam introduced by prompt 11. Callers that do not inject keep today's behaviour; the default must be named and documented.
3. Document the availability rule as a caller contract and name the composition-root line that enforces it, so the comment and the enforcement are one statement.
4. Keep and cite the already-documented argv-only formatting and head/tail truncation contract instead of re-describing it; add a pointer to it from the new table.

## Non-goals

- Do not change the build-command selection order or multi-command semantics.
- Do not change truncation budgets.
- Do not change how the tool is described to the model.
- Do not touch the build-config parser, which is correct and tested.

## Acceptance

- The declared build command succeeds.
- A new `build_tool_tests` target exists and compiles only this unit, the injected runner's unit and the test utilities.
- With a fake runner injected, it asserts: a declared single command that succeeds yields ok with the documented keys and exit status; a failing command yields the failed-command code and the exit code of that specific command; a timed-out command yields the timed-out flag; output beyond the budget is truncated with head and tail preserved as documented; an unknown command index yields the named code; a workspace with no build commands reports not-available with its named code.
- Nothing in the target starts a real shell; the existing real-shell scenario is marked INTEGRATION or deleted, and the report says which.
