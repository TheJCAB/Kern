# Fix: src/SessionId

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: remove non-determinism from a construction path and delete a translation unit that exists for one function.

## What is wrong

1. There is no session-id type despite the filename; the unit is a generate function plus an aggregate.
2. The aggregate's id member has a default member initialiser that draws entropy, so merely constructing the type anywhere — including inside test helpers in other units' tests — pulls a random device.
3. The alphabet, length and uniqueness expectation are undocumented beyond "fresh random identifier".
4. The implementation file is 15 lines.

## What to change

1. Move the generate function into the header as an inline function and delete the implementation file, leaving one unit.
2. Remove the default member initialiser so the id must be supplied explicitly at every construction site, and fix each site to generate one where generation is genuinely wanted; list the sites you changed.
3. Document the format: length and alphabet, that collision resistance is why it is used, and what it is NOT suitable for; do not claim cryptographic strength unless it is true of the source used.
4. Ensure every test that constructs the aggregate passes a literal id rather than generating one.

## Non-goals

- Do not change the id format or length.
- Do not add a new abstraction layer or interface over id generation; that is a different, larger change, so say so and leave it.
- Do not change the session recording schema.
- Do not touch `SessionRecorder`'s build id.

## Acceptance

- The declared build command succeeds.
- `src/SessionId.cpp` no longer exists and no target lists it.
- A grep shows no test constructing the aggregate without an explicit id.
- A NEW or EXISTING test target asserts that two generated ids differ across repeated calls in one process; state that this is a weak assertion and why it is the best one available.
- A generated id matches the documented shape (length and alphabet); this is the strong, deterministic assertion.
- Supplying an explicit id never generates one.
