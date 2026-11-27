# Fix: src/StringUtilities

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: take the platform console side effect out of the text-helper unit, and replace the one test that asserts nothing.

## What is wrong

1. A Windows console-encoding configuration function, which registers an `atexit` handler, lives among eight pure text helpers, so the unit carries two unrelated concerns.
2. That function's only test calls it and checks that it does not crash; it exposes no observable contract, so it is effectively untested.
3. One helper's assertion accepts a range of lengths instead of the exact expected string, a weak assertion in a unit that is otherwise fully testable.

## What to change

1. Move the console configuration into its own header, and an implementation file if it needs one, named for the console concern. This move is MOVE-ONLY: leave the text and line helpers where they are.
2. Document the console unit as a platform side effect that is intentionally unasserted on non-Windows hosts, stating exactly what it sets and that it runs once per process.
3. Replace the weak length assertion with the exact expected string.

## Non-goals

- Do not change any text helper's behaviour, including newline handling and escaping rules.
- Do not add a doc-comment style the repo lacks.
- Do not split the remaining text helpers further; the review found them cohesive once the console function leaves.
- Do not change call sites other than including the new console header.

## Acceptance

- The declared build command succeeds.
- The console header and implementation file exist, and this unit no longer contains platform code: read the file and confirm there is no `windows.h` and no `atexit`.
- `string_utils_tests` compiles only this unit plus what it truly needs, and still passes.
- A new or existing test asserts each text helper against literal expected strings, including the helper whose assertion was previously a length range.
- The test file carries a comment stating that the console unit is exercised only by the composition root, and why.
