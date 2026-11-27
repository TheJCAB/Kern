# Fix: src/LogFile

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md` and `01-session.md`, which removes the in-loop call; if either has not landed, stop and say so.
Goal: make the per-request log writer honest about its location, non-fatal on write failure, and testable.

## What is wrong

1. The default directory is resolved once per process into a cached static, and directory-creation errors are discarded, so nothing can redirect it and nothing reports failure.
2. `Write` throws from inside the turn loop, which aborts the whole turn for what is diagnostic output — unlike the session recorder, which is best-effort.
3. The unit has no test at all, and its file-naming policy duplicates the recorder's with no shared helper.

## What to change

1. Keep the default-directory convenience but make the constructor that takes a directory the primitive, and make the static cache explicitly lazy and documented as process-wide, so a test always uses the explicit-directory constructor.
2. Stop throwing on write failure; report once through the log and continue, matching the recorder's best-effort policy, and document that choice.
3. Document the naming scheme next to the code that builds it.
4. If the naming policy is now identical to the recorder's shape, extract one small path-building helper and use it in both places, or state why the two must differ.

## Non-goals

- Do not merge this into the session recorder; they serve different lifetimes and the review found no duplication of purpose.
- Do not change what is written per request.
- Do not add file rotation.
- Do not change the recorder's schema; that is prompt `22-session-recorder.md`.

## Acceptance

- The declared build command succeeds.
- A NEW `log_file_tests` target exists that constructs the writer with an explicit temp directory and asserts the following.
- A write creates the expected file with the expected name and contents.
- Two writes with different sequence numbers create two distinct files.
- The directory is created if absent.
- A write into an unwritable path reports and returns instead of throwing; state how you forced it portably, or say you could not and test the reporting path directly.
- The default-directory static is never touched by any test.
