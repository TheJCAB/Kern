# Fix: src/Session

## Status

Unstarted.

## Prerequisite

None; the transport is supplied by the caller rather than referenced from `Session.cpp`, so this prompt is self-contained.

## Goal

Make `src/Session` testable without a real network, a real log directory or a terminal, and replace the flag-and-`continue` turn loop with control flow composed of named function calls.

## What is wrong

1. `Pimpl::RunPrompt` at `src/Session.cpp:99-358` is one `while (true)` loop carrying payload assembly, response normalisation and merge, tool dispatch, transcript recording and three recovery policies at once, so no stage can be named or tested alone.
2. The flat loop forces `TokenLimitRecovery`, `ErrorRecovery` and `TurnLimitRecovery` plus the `reportTurnOffered` flag (declared at `src/Session.cpp:115-121`) to act as a hand-rolled call stack: their `Messages()` are spliced into the next request's payload at `src/Session.cpp:146-148`, and `Exhausted()`/`WrapUpPending()` decide control flow. The budget check at `src/Session.cpp:127-136` is a guard plus `continue` for the same reason.
3. The `try` at `src/Session.cpp:141` spans through line 336, though the comment at line 339 says it is for a failed request. It therefore also catches the deliberate throws at lines 279 and 334 and any exception from `CallTool` at line 305, so an unknown tool name is retried up to `ErrorRecovery::MaxRetries` and surfaces as `request_failed`.
4. `std::println` appears at lines 106, 140, 203, 246, 270, 278, 303, 307, 323, 333 and 341, so the observable contract includes terminal prose a test cannot inspect.
5. The reference returned by `Id()` points into the pimpl and dangles once the session is destroyed; the declaration at `src/Session.h:61` does not say so.

## What to change

1. Extract the response normalisation and merge block (`src/Session.cpp:219-262`, including the XML merge into `toolCalls`) into one namespaced free function `SessionDetail::NormaliseResponse`, declared in `src/Session.h` and defined in `src/Session.cpp`, returning `SessionDetail::NormalisedResponse`, a struct holding the normalised `std::vector<ToolCall> toolCalls`, the effective `ResponseFinishReason reason` and the `std::string assistantContent` to append. Inputs: the `ModelResponse`, the `ToolsRuntimeContext`, the owning session id and the tool span the XML parse needs. Do not fold payload building, dispatch or logging into it. Give the function external linkage: the existing helpers at `src/Session.cpp:33-65` are `static` members of `Pimpl` and cannot be linked by a test.
2. Compose control flow from named single-purpose functions instead of flags and `continue`. Each stage returns a small named struct with an `enum class` outcome field — not a `bool`, not a `std::pair`, not a `std::tuple`, and call sites must not contain `.first` or `.second`. The turn-budget and report-turn case at `src/Session.cpp:127-136` becomes a named function (`CheckTurnBudget`) rather than a guard at the top of the loop. Mark every stage's boolean failure signal `[[nodiscard]]`. Each stage does real work; a stage that only forwards its arguments is not a stage.
3. Give the turn loop one epilogue that builds `PromptResult` from the accumulated outcome, so `stoppedAtTurnLimit` and `turnsUsed` cannot be omitted on a path; they are omitted today at lines 324, 346 and 354. Do not change turn-budget behaviour or any `errorCode` value.
4. Narrow the `try` at line 141 so it covers only the request and the normalisation. The deliberate protocol-violation throws at lines 279 and 334 and `CallTool` failures at line 305 must not be mistaken for transient request failures. An unknown tool name must yield an error tool result in the conversation and must never become `request_failed`; if `CallTool` throws on an unknown name, catch it at the dispatch site and convert it to that result.
5. Declare the transport seam in `src/Session.h` as a named alias `using StreamedRequest = std::function<ChatTransport::Exchange(Endpoint const&, nlohmann::json const&, LogFile&)>`, and take it as a required `Prompt` parameter with no defaulted argument, following `src/SubagentSessions.h:101` and its `Run` signature at line 107, whose implementation is the caller-built lambda in `src/main.cpp:237` passed at line 254. Supply the `ChatTransport::SendStreamedChatRequest`-backed implementation from `src/main.cpp`. `src/Session.cpp` must no longer name `ChatTransport::SendStreamedChatRequest`; `src/ChatTransport.h` may still be included for the `Exchange` type, but no symbol from `ChatTransport.cpp` may be referenced.
6. Replace the `std::println` calls inside the loop with an injected output sink (`std::function<void(std::string_view)>`, also a required `Prompt` parameter, supplied by `src/main.cpp`), or delete the ones that only duplicate what `LogFile` already records. No `std::println` remains inside the turn loop.
7. Document the lifetime of `Id()` in a comment directly above its declaration at `src/Session.h:61`: the returned reference is owned by the session and dangles once the session is destroyed.
8. Write the extracted stages so they introduce no unused parameter, unused local, dead or unreachable branch, or cast whose only purpose is to silence a diagnostic.

## Non-goals

- Do not change the payload format or the order of messages sent to the model.
- Do not change turn-budget behaviour.
- Do not move `Session` to a new file or split it into several translation units.
- Do not add or rename `Config` fields: `Config::sessionDirectory` at `src/Session.h:45` already exists and `src/Session.cpp:78` already builds `m_log` from it via `MakeRecorderDirectory` (`src/Session.cpp:65-68`), so reuse it and do not re-plumb the log directory.
- Do not change `SessionRecorder`'s schema.
- Do not change the tool dispatch table.
- Do not convert the loop to a coroutine; that is out of scope.

## Acceptance

- The workspace's build command succeeds.
- A new test target `session_tests` exists in `tests/CMakeLists.txt`, following the existing per-test pattern there (explicit `add_executable` source list, `target_include_directories(... ../src)`, `target_link_libraries(... TestUtilities)` plus `nlohmann_json::nlohmann_json` where needed, then `add_test`), compiling `../src/Session.cpp` plus only the units it exercises.
- The `session_tests` link line contains no networking unit and no `ChatTransport.cpp`.
- `session_tests` substitutes both the transport (a `std::function` returning a canned `Exchange`) and the log directory (a temporary directory supplied through `Config::sessionDirectory`), with no socket opened and no write to the user's real log directory.
- Assert: a single-turn prompt appends the user message then the assistant message, in that order.
- Assert: a response carrying two tool calls yields two normalised calls.
- Assert: an unknown tool name yields an error tool result in the conversation and does not produce `errorCode == "request_failed"`.
- Assert: the loop stops at a terminal finish reason.
- The extracted normalisation function is tested directly with a table of rows, each row naming its input and its expected normalised calls.
- Asserts use the hand-rolled `Expect` and `ExpectEqual` in `tests/TestUtilities.h`.
- Anything that could not be asserted is listed in the result, with the reason.
