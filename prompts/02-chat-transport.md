# Fix: src/ChatTransport

Status: unstarted.
Prerequisite: `03-network-utilities.md` and `00-build-graph-and-unit-targets.md`; if either has not landed, stop and say so.
Goal: make `src/ChatTransport` a named, testable unit that owns the streamed request and its retry policy and nothing else.

## What is wrong

1. The header declares a constructor that is never defined and never used — the only caller uses the static entry point, so the type is a namespace with a link error waiting.
2. Retries sleep real wall-clock time with no clock or delay seam, so any test must actually wait.
3. Streamed content is rendered to stdout with `std::print` during assembly, so the terminal is part of the contract.
4. The file mixes transport, retry policy, terminal rendering, tool-call assembly and a commented-out predecessor block.
5. The header's class-level prose is good but describes behaviour the class cannot be constructed to perform.

## What to change

1. Delete the undefined constructor, or define it and make the static entry point a thin wrapper over an instance; state which you chose and why in a comment above the declaration.
2. Inject the delay as a `std::function<void(std::chrono::milliseconds)>` call parameter defaulting to `std::this_thread::sleep_for`, so a test passes a no-op.
3. Inject the output sink the same way (a `std::function<void(std::string_view)>`), defaulting to the current printing behaviour, and route every `std::print`/`std::println` in the file through it.
4. Delete the commented-out predecessor block entirely.
5. Move the retry constants to named values near the top of the file with a comment stating the backoff shape.

## Non-goals

- Do not change the SSE parsing itself (that is prompt 03).
- Do not change the `ModelResponse` assembly (prompt 05).
- Do not change `Session`'s call site beyond matching the new signature.
- Do not alter logging output format.

## Acceptance

- The declared build command succeeds.
- A NEW `chat_transport_tests` target exists and compiles `ChatTransport.cpp` plus only what it needs; its source list contains no `.cpp` it does not exercise.
- With both seams injected, a retry policy that returns success on the second attempt makes exactly two network calls and one delay of the expected duration.
- With both seams injected, a failure after the maximum retries returns the failure rather than looping.
- With both seams injected, the assembled text of a scripted sequence of SSE frames equals the expected full string.
- With both seams injected, the sink receives exactly the deltas printed today, in order, with no terminal write.
- Note in the prompt record any assertion you could not make without a live server and why.
