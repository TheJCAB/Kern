# Fix: src/SessionRecorder

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md` and `09-log-file.md`; if either has not landed, stop and say so.
Goal: make the "recording disabled" flag honest across the whole recording surface, and stop baking build time into the artefact.

## What is wrong

1. Disabling recording silences only this unit, while the per-request log writer and the per-turn record still write their dumps, so "off" still writes files — a false promise a user can observe.
2. The build identity is a compile-time date and time string, so recordings from one build are indistinguishable from another's, and the value cannot be injected.
3. The unit is otherwise the best-tested persistence unit, so the fix is small and contained.

## What to change

1. Make the disable flag authoritative for the whole recording surface — the recorder, the per-request log writer and the per-turn record — by having each consult one decision: a settings value passed in, never a global. Document in the header that no file is written when recording is off.
2. Make the build identity an injectable value, keeping the compile-time constant as the documented default.
3. State in the header which files are written when recording is on and which are not, as a short list.

## Non-goals

- Do not change the transcript schema, and change no manifest field other than the source of the build identity.
- Do not merge the per-request log writer into this unit.
- Do not add rotation or retention.
- Do not change what a turn record contains; prompt 23 owns that.

## Acceptance

- The declared build command succeeds.
- With recording disabled, no file is created in the target directory by any of the three writers: count the directory entries before and after a full simulated turn and assert the counts are equal.
- With recording enabled, the documented files appear.
- An injected build identity appears verbatim in the manifest.
- The default path still compiles and is asserted to be non-empty.
- Prove that nothing was written by listing the directory before and after the simulated turn, not by trusting the writers' return values, and record that listing in the test output.
