# Fix: src/Logging

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: give the log a substitutable sink so its format and gating can be tested, and document the thread-safety of its global level.

## What is wrong

1. The level is a mutable global mutated by a setter, and the thread-safety of that variable is undocumented while many translation units link it.
2. The message writer goes straight to stderr with no sink, so the only testable parts are level parsing and the enabled check; the output format and level names are unverified.
3. The header's contract is one line, and the "never log secrets or headers" rule is a calling convention with no statement of where it applies.

## What to change

1. Add an injectable sink — a `std::function` defaulting to the current stderr write — accepted by the logging entry point or settable once at startup, and document which of the two it is.
2. Document the thread-safety guarantee of the level: state plainly whether it is safe to set concurrently with logging, and if it is not, say so and describe the intended startup-only use.
3. Document the message format (fields, separators, level rendering) above the writer, and make that format live in a single place in the code.
4. Document the secrets rule once, naming what must never be passed in, and add a short comment at each call site that passes request data saying whether it is already sanitised or that it must be.

## Non-goals

- Do not adopt a logging library.
- Do not change the on-disk or on-screen format other than documenting it.
- Do not make the level atomic unless you also document it; a documented startup-only rule is acceptable.
- Do not change `Log` template users' call sites beyond those that pass request data.

## Acceptance

- The declared build command succeeds.
- The existing `logging_tests` target gains assertions through the injected sink.
- A message at the enabled level is emitted once with the documented format; assert the structured fields, not a substring of prose.
- A message below the level is not emitted.
- Parsing every documented level name round-trips, and an unknown name yields the documented fallback.
- The level name rendering is asserted directly.
- State whether the global level remains untestable in parallel and how you documented that.
