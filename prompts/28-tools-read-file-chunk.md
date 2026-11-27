# Fix: src/Tools/read_file_chunk.h

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`, `24-tool-utilities.md`, `17-file-utilities.md`; if any has not landed, stop and say so.
Goal: fix a documented default that the code contradicts, and make the truncation rule knowable from the header.

## What is wrong

1. The `end_line` parameter is declared with a default of 1 in the schema while the body defaults to the maximum 64-bit integer, so the documented default contradicts the behaviour: a model following the schema asks for everything and gets one line.
2. The truncation rule — when the chunk reports a cap because the end line was clamped — is undocumented, so the boundary case, end of file exactly on the cap, cannot be predicted.
3. It returns an ad-hoc result with a bare error string and no error code.
4. There is no header comment for the tool at all; only the model-facing description carries the contract.

## What to change

1. Decide the correct default — whole file from line 1, which is what the body already does — and make the schema, the body and the documentation agree on it.
2. Document the truncation rule precisely, including the exact-boundary case, in a comment above the tool.
3. Converge the result on the shared envelope with named error codes for: unknown root, not a file, and out-of-range start. That envelope's single definition lives with the dispatch unit from prompt 24 and is shared, not copied per tool.
4. Add the unit colophon.

## Non-goals

- Do not change the maximum line cap.
- Do not change the parameter names.
- Do not add streaming or byte offsets.
- Do not change the file-validation rules owned by prompt 18.

## Acceptance

- The declared build command succeeds.
- A new `read_file_chunk_tests` target exists and compiles only this unit.
- Asserted through a fake or temporary file: omitted bounds return the whole file; an explicit start and end return exactly those lines; a start beyond the end of file yields the named error code; a file longer than the cap returns the cap and the documented truncated flag; a file whose last line lands exactly on the cap boundary has the documented outcome; an unknown root and a directory each yield their named codes.
- The declared default is asserted equal to the body's default by reading the schema in the test and comparing it with the observed result.
