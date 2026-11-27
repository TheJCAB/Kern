# Fix: src/ModelResponse

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: make the response-parsing unit live up to its name: parsing in, no dead code, and one documented failure policy.

## What is wrong

1. The non-streamed `ExtractModelContent` is dead — its only call site is commented out, so the non-streamed contract is neither exercised nor tested.
2. The parse functions do IO, debug-logging and print the whole response body, so "parse" is a lie.
3. The delta parser tolerates an unknown or missing finish reason while the full-response parser throws, an asymmetry nothing documents.

## What to change

1. Delete `ExtractModelContent` and its declarations, OR restore a caller and test it — pick one and say which in the prompt's own record; if deleting, also delete the now-unused helpers it needed.
2. Remove the `std::println` of the whole response body; keep debug logging only behind the existing log-level check and state the level.
3. Document the asymmetry above the two declarations: one tolerates an unknown finish reason, the other throws, and why.
4. Document what happens with a missing `content`, a missing `tool_calls` and an unknown role.

## Non-goals

- Do not change the wire model or field names.
- Do not move parsing out of this unit into the transport.
- Do not change `ChatTransport`'s assembly logic beyond removing references to a deleted function.
- Do not touch error-recovery policy.

## Acceptance

- The declared build command succeeds.
- A NEW `model_response_tests` target exists that compiles only `ModelResponse.cpp` plus its dependencies.
- A text-only JSON body parses to the expected content with no tool calls.
- A tool-call body parses to the expected calls with ids preserved.
- An unknown finish reason maps to the documented outcome for the delta path and throws (with a message naming the value) for the full path.
- A body missing `content` yields empty content rather than an exception.
- Parsing a body produces no stdout.
- If you deleted `ExtractModelContent`, assert that a grep finds no remaining reference and list the files you checked.
