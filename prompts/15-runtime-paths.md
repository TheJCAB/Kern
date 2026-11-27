# Fix: src/RuntimePaths

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: make the once-per-process layout resolution testable by naming it, and by removing the self-referential assertions in its test.

## What is wrong

1. The layout is resolved once into a cached static, so only the first branch can ever be exercised in-process and the installed-layout and user-state fallbacks are never reached.
2. The existing test recomputes its expectations with the same helper under test, so it can pass while both are wrong.
3. The test target also links units it does not test.
4. The environment fallback duplicates the policy now also in the services unit.

## What to change

1. Separate the pure resolution decision — inputs: executable directory, whether an install-relative data directory exists, the relevant environment values; output: the chosen data directory and the reason — from the caching wrapper, and document the cache as explicit, process-wide and test-only-avoidable.
2. Rewrite the test to assert against literal expected paths derived from an explicitly injected directory, not against a value recomputed by the unit.
3. Drop the units the target does not test; this is completed by the build-graph work, so verify rather than assume.
4. Point the environment fallback at the shared helper introduced by the services prompt.

## Non-goals

- Do not remove the environment fallback.
- Do not change the directory names or the install-relative layout.
- Do not change when the cache is populated in production.
- Do not add a filesystem abstraction.

## Acceptance

- The declared build command succeeds.
- The test target compiles only this unit plus what it truly needs.
- Assertions cover the pure decision function for the build-tree layout, the installed layout, and the user-state fallback, each with an injected input and a literal expected path.
- An assertion exists that the executable-directory helper is not used to compute an expectation, checked by reading the test.
- The documented precedence order is asserted with two inputs that would produce different answers if the order flipped.
- State which branches remain unreachable in-process and why the extraction makes them testable anyway.
