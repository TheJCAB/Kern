# Fix: src/Session

Status: unstarted.
Prerequisite: `03-network-utilities.md` and `02-chat-transport.md` define the transport seam this prompt consumes; if they have not landed, stop and say so.
Goal: make `src/Session` a unit that can be tested without a real network, a real log directory, or a terminal, and reduce its oversized turn function to named stages.

## What is wrong

1. `RunPrompt`/`Prompt` in `src/Session.cpp` is roughly 250 lines fusing the turn loop, payload assembly, tool-call merging and normalising, tool dispatch and transcript recording into one function, so no stage can be named, read or tested in isolation.
2. There is no injection seam: `ChatTransport::SendStreamedChatRequest` is called directly and `LogFile::Directory()` is reached directly from inside the loop, so a test cannot reach the loop at all without a real network and a real log directory.
3. `std::println` writes happen at several points in the loop, so the observable contract of `Session` includes terminal prose that a test cannot inspect and that a headless run only pollutes.
4. The contract of `Id()` — that it returns a reference into the pimpl which dangles once the session is destroyed — is unstated in the header, so callers keep the reference past the owner's lifetime.

## What to change

1. Extract the tool-call normalisation and merge block out of `RunPrompt` into a file-local free function in `src/Session.cpp` with a testable signature, declared in `src/Session.h` so a test can call it directly. Input: the model's tool-call deltas and the current conversation. Output: a small struct holding the normalised calls plus the assistant message to append. Name it and name the struct; do not fold any other responsibility into them.
2. Make the transport injectable by taking a `std::function` for "send one streamed request and return the assembled response", defaulting to the existing `ChatTransport` entry point. Pass it as a call parameter, not as a stored constructor member, following the pattern already used for the subagent factory in `src/SubagentSessions.h`.
3. Make the log directory a constructor or config parameter instead of reaching for `LogFile::Directory()` inside `Prompt`. Add the member to `Config` under a new name; do not rename existing fields.
4. Replace the direct `std::println` calls in the loop with the injected output sink introduced by the transport prompt, or delete the prints that only duplicate information already written through `LogFile`. No `std::println` remains inside the turn loop.
5. Document `Id()`'s lifetime in the header, in a comment directly above the declaration: state that the returned reference is owned by the session and dangles once the session is destroyed.

## Non-goals

- Do not change the conversation payload format or the order of messages sent to the model.
- Do not change turn-budget behaviour; the ceiling belongs to prompt 21.
- Do not move `Session` to a new file or split it into several translation units.
- Do not change `Config` field names other than adding the injected members.
- Do not touch `SessionRecorder`'s schema or its record format.
- Do not change the tool dispatch table owned by prompt 24.

## Acceptance

- The declared build command succeeds.
- A NEW test target `session_tests` exists and compiles `Session.cpp` plus only the units it needs; its source list contains no `.cpp` the test does not exercise.
- `session_tests` substitutes BOTH the transport and the log directory with fakes: a `std::function` returning a canned response, and a temporary directory, with no socket opened and no write to the user's real log directory.
- `session_tests` asserts that a single-turn prompt appends the user message and the assistant message in order.
- `session_tests` asserts that a response containing two tool calls produces two normalised calls.
- `session_tests` asserts that an unknown tool name produces an error tool result rather than an exception escaping.
- `session_tests` asserts that the turn loop stops at a response with a terminal finish reason.
- The extracted normalisation function is tested directly with a table of inputs, each row naming its input deltas and its expected normalised calls.
- Any of the above that could not be asserted is listed in the prompt's result, with the reason.
