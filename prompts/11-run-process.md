# Fix: src/RunProcess

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: keep the one-function public surface, but make the two things a test needs substitutable and make the platform split explicit.

## What is wrong

1. The unit is one free function whose 393 lines hide fork/pipe/poll/kill on POSIX and a separate implementation on Windows, so no test can substitute an executor and one platform branch is always dead.
2. The deadline/poll/timeout arithmetic is inline after the fork, so it cannot be tested without a real child process.
3. The header documents the contract well, so the only gap is the seam, not the prose: nothing says which platform is compiled in or what a test should call.

## What to change

1. Extract the timeout/deadline arithmetic into a named pure helper: the clamp computation and the decision of when to stop polling or kill. Give it a comment stating the units (milliseconds) and the clamp bounds, and unit-test that helper instead of the process.
2. State in the header that the function reaches the real process API, that exactly one platform branch is reachable per host and the other is not compiled or not taken, and name the helper a test should exercise.
3. Do not add an executor interface unless the extraction above proves insufficient; if you do add one, say why in the header comment.

## Non-goals

- Do not change the public signature of the run function.
- Do not add threads or async.
- Do not unify the two platform implementations into one.
- Do not change output truncation limits.
- Do not touch the build tool's callers beyond keeping them compiling.

## Acceptance

- The declared build command succeeds.
- A new `run_process_tests` target exists and asserts, without spawning a long-lived process where possible:
  - the extracted clamp/deadline helper with a table of inputs including zero, negative, a value over the maximum, and the maximum itself;
  - the unit still runs one trivial, fast, portable command to completion and reports its output and exit code — mark this single assertion `INTEGRATION` in a comment, and skip it on platforms where no portable command exists, naming them;
  - a command that exceeds its timeout reports timed out rather than hanging.
- State explicitly which assertions remain unverifiable on the host you ran them on.
