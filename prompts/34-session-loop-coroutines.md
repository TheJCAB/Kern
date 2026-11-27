# Session loop: coroutine promotion

## Status

Decision record, no code changes; independent of whether `01-session.md` has landed.

## Question

Should the `src/Session` turn loop be rewritten as a C++ coroutine, and if not now, under exactly what conditions should it be?

## Current shape

- `Pimpl::RunPrompt` is `src/Session.cpp:99-358`, built around one `while (true)` at line 123.
- State carried across iterations is the pimpl members `m_conversationHistory` (`src/Session.cpp:407`), `m_currentTurn` (line 405), `m_toolContext.turnEpoch` (set at line 161), `m_tools` (line 402) and `m_maxTurns` (line 404), plus the loop-scope locals `tokenLimitRecovery`, `errorRecovery`, `turnLimitRecovery` and `reportTurnOffered` declared just above the loop at `src/Session.cpp:115-121`.
- `ChatTransport::SendStreamedChatRequest` in `src/ChatTransport.h:28` returns `ChatTransport::Exchange` by value after `HttpPost` has drained the SSE stream synchronously (`src/ChatTransport.cpp:32`), and `CallTool` in `src/ToolUtilities.h:157` returns `std::string` on the calling thread, so neither the request nor dispatch suspends.

## The argument for it

- A coroutine is a compiler-performed transformation of an explicit state machine into direct control flow, so the choice is about readability and not about I/O.
- The three recovery policies exist in their present shape only because a flat loop has no call stack to hold the nesting, and a coroutine would let a recovery be a nested await that sends a temporary request carrying the error and returns a response the outer loop treats as this turn's, which collapses the current `Messages()` splice at `src/Session.cpp:146-148` and the `Exhausted()`/`WrapUpPending()` control flow into ordinary calls.
- With the loop composed from named functions per `01-session.md`, the remaining state a frame would hold is small.

## The argument against it now

- Every await would be on an already-complete operation, so a frame would allocate and suspend zero times to buy a control-flow style.
- `Session::Prompt` is a public non-template member returning `PromptResult` (`src/Session.h:57`), so a coroutine changes the return type and every caller.
- A promise type and awaiter would want a header of their own.
- An exception thrown inside a coroutine must be caught and rethrown at the resume point or the existing handler around `RunPrompt` (`src/Session.cpp:367-380`) silently stops working.
- A suspended frame holding `this` into a movable `unique_ptr` pimpl (`src/Session.h:70`) compounds the lifetime problem `01-session.md` item 7 fixes.
- An exhaustive `switch` over an outcome enum is compiler-checked and greppable, whereas nothing tells a reader where a frame's resume points are.

## Decision

Compose the loop from named functions now; promote to a coroutine only when a real suspension exists, namely an asynchronous transport, tool dispatch that awaits, or concurrently running subagent sessions completed asynchronously, which would let one single-threaded event loop hold many suspended session frames with no threads and no locks.

## What keeps promotion cheap

- The named `StreamedRequest` alias from `01-session.md` item 5, which promotion changes to a task-returning signature.
- The single `PromptResult` epilogue from `01-session.md` item 3.
- The per-stage outcome type from `01-session.md` item 2.
- Supply of the transport by `src/main.cpp` (`src/main.cpp:237`, passed at line 254) rather than by `src/Session.cpp`.

## Questions to answer

1. Does the promise type drive to completion on the calling thread, or does it become genuinely awaited at the seam?
2. Do turns spent inside a nested recovery request increment `m_currentTurn`, and how are they billed to the transcript?
3. How is `TurnRecord` nesting recorded for a recovery request, given one `TurnRecord` is constructed per iteration at `src/Session.cpp:139`?
4. Who owns the frame if a session is destroyed mid-flight?
5. Do the three recovery policy classes survive as message providers only?
6. What does the test harness look like when a scripted transport can fail, retry and succeed in sequence?

## Deliverable

A written decision (adopt, defer or reject) naming the trigger condition that would change it, and, if adopt, an ordered change list. No code.
