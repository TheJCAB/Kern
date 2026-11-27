# Fix: src/Tools/delete_file.h

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`, `24-tool-utilities.md`, `18-file-validation.md`; if any has not landed, stop and say so.
Goal: replace two coexisting error shapes with one envelope and a named error-code vocabulary.

## What is wrong

1. The tool returns one shape for a missing path or a directory and a different shape for a caught exception, and neither carries an `errorCode` or an `ok` flag, so a caller must inspect which keys are present to know what happened. This is the clearest case of the ad-hoc convention in the tool cluster.
2. Deletion is a raw filesystem call placed next to validated paths, so a test double would need both a real directory and a validator root to exercise one branch.
3. The model-facing description promises refusals without naming the codes the caller will actually see, so a model cannot tell a protected path from a missing one.

## What to change

1. Converge both the pre-check failure path and the caught-exception path onto the shared envelope. Add named codes to the vocabulary for: unknown root, protected path, path is a directory, path does not exist, and deletion failed. That envelope's single definition lives with the dispatch unit from prompt 24 and is shared, not copied per tool — do not declare a local copy here.
2. Route the deletion through the same validated path the check returned, so the operation and the check cannot disagree about which file is removed.
3. Rewrite the model-facing description so it names each refusal it can produce, using the same words as the codes, and says that nothing is removed when the call is refused.
4. Add the unit colophon above the includes, in the form given by `reviews/03-documentation-and-caller-contracts.md`: purpose, what the unit owns and does not own, invariants, what the caller must do, and the test seam.

## Non-goals

- Do not add recursive deletion or a force flag.
- Do not change which paths are protected.
- Do not change the parameter schema.
- Do not touch the validator's own rules, which prompt 18 owns.

## Acceptance

- The declared build command succeeds.
- A new `delete_file_tests` target exists in `tests/CMakeLists.txt` and compiles only this unit, the shared envelope's unit and the test utilities.
- Asserted against a temporary directory with a validator root: deleting an existing file removes it and reports ok with the documented status.
- A path outside every root yields the unknown-root code, not a raw filesystem error.
- A protected path is refused with the protected code and the file still exists afterwards.
- A directory path yields the directory code and nothing is removed.
- A nonexistent path yields the not-found code.
- The key set of the result is identical across all of those cases; that assertion is what proves the two-shape defect is gone, and the test file must say so in a comment.
