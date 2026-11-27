# Fix: src/Tools/write_file.h

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`, `24-tool-utilities.md`; if either has not landed, stop and say so.
Goal: stop reporting a line count computed from the model's input rather than from what was written, and give a 54-line tool the three assertions it deserves.

## What is wrong

1. The reported number of lines is computed from the content string, not from what landed on disk, so a write that is transformed or truncated still reports the requested count, and nothing says so.
2. The created flag is computed with a raw existence check on a path that was not the validated one used for the write, so the two inputs disagree.
3. The only coverage is incidental, inside another unit's target, and asserts a size rather than the semantics.
4. The tool is the smallest untested unit in the cluster, which makes its absence of tests the clearest sign that there was no tool-test harness at all.
5. Its result is ad-hoc, with a bare error string and no error code, unlike the units that already follow the shared envelope.

## What to change

1. Compute the reported line count from the bytes actually written, or re-read the file, and document which of the two it is.
2. Use the same validated path for the existence check and for the write, and fail with a named error code if validation and the write target disagree.
3. Converge the result on the shared envelope with named codes for: unknown root, not writable, and a parent directory that does not exist — deciding, and documenting, whether to create that parent or refuse it. That envelope's single definition lives with the dispatch unit from prompt 24 and is shared, not copied per tool.
4. Add the unit colophon.

## Non-goals

- Do not add appending or partial writes.
- Do not change the creation-versus-overwrite semantics.
- Do not change the parameter schema.
- Do not touch the line-counting text helper's behaviour, which prompt 16 owns.

## Acceptance

- The declared build command succeeds.
- A new `write_file_tests` target exists and compiles only this unit.
- Asserted on a temporary directory: writing a new file creates it and reports created true with the documented line count; overwriting an existing file reports created false.
- The reported line count equals the number of lines actually on disk, asserted by re-reading the file, so this test must fail if the count is computed from the input.
- A trailing-newline-only difference is handled as documented; an unknown root and an unwritable path each yield their named codes; a write whose parent directory is missing behaves as documented.
