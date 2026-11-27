# Fix: src/main.cpp

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`, `21-turn-limit-recovery.md`; if either has not landed, stop and say so.
Goal: move the one piece of pure policy hiding in the composition root into a testable unit, and make protected-path registration assertable.

## What is wrong

1. The `/continue` argument parser — token splitting, integer validation, the "positive whole number only" rejection and the resume-prompt text — is pure string-in, text-out policy, inline in the composition root and untested while the grammar it belongs to is tested.
2. The runtime-context builder registers the build declaration file and the version-control directory as protected, which is real policy inside a function reachable only by running the whole program.
3. The composition root is otherwise correctly a composition root and must stay one.

## What to change

1. Extract the `/continue` parsing into a named unit with a documented result type distinguishing four outcomes: no argument, an accepted non-negative integer, a non-integer, and an out-of-range value — plus the formatted resume prompt for the accepted case. Keep the wording of that prompt and the rejection message byte-for-byte as today.
2. Extract the protected-path registration list into a named function in the file-validation or context unit, so its contents are assertable without starting the program. It returns or takes the list; it must not gain a second source of truth.
3. Have the composition root call both and hold no logic of its own beyond wiring: no token splitting, no integer conversion, no format string.
4. Add the colophon to the new unit, per `reviews/03-documentation-and-caller-contracts.md`.

## Non-goals

- Do not restructure the rest of the composition root.
- Do not change the grammar of any other command.
- Do not change which paths are protected, only where the list lives.
- Do not add a command-line parsing library.

## Acceptance

- The declared build command succeeds.
- The composition root contains no token splitting, no integer conversion and no resume-prompt format string; verify by reading it and quote the lines that replaced them.
- A new target exists and compiles only the parser unit, the protected-path unit and the test utilities.
- It asserts: an absent argument yields the no-argument result; a bare integer yields that value; fractional, negative and zero values are rejected with the documented distinction between malformed and out-of-range; leading, trailing and repeated whitespace are handled as documented; the accepted case produces the documented resume prompt, asserted against a literal.
- The protected-path function returns the expected list, including the build declaration and the version-control directory, asserted by name.
