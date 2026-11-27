# Fix: src/Tools/edit_file_lines.h

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`, `24-tool-utilities.md`; if either has not landed, stop and say so.
Goal: fix a header whose includes are wrong, and move its existing, already good coverage into a target with its own name.

## What is wrong

1. The header includes only one project header yet uses filesystem paths, a 64-bit integer type and formatted strings, so it compiles only because a consumer happens to include the right headers first; its dependency list is not merely undocumented, it is false.
2. The unit is exercised only inside a target named for a different concern, so the only tool with substantial behavioural tests hides them under a misleading name.
3. The line-range arithmetic is the part most worth testing directly, and it is file-local.
4. The result is assembled locally rather than through the shared envelope, so it carries ad-hoc keys and a bare error string instead of error codes.

## What to change

1. Add the standard and project includes the header actually requires so it compiles alone, and verify that by compiling it alone.
2. Make the line-range construction reachable from the unit's test; keep it file-local only if the tool's own tests can still assert every range boundary through the tool.
3. Move this unit's scenarios out of the other target into a target named after this unit, and leave the other target testing what its name says.
4. Converge the result on the shared envelope: ok/status/errorCode with the stable named error-code vocabulary. That envelope's single definition lives with the dispatch unit from prompt 24 and is shared, not copied into this tool.
5. Add the unit colophon.

## Non-goals

- Do not change the tool's parameter schema or its documented error codes.
- Do not change insertion or replacement semantics.
- Do not add a new abstraction over the filesystem.
- Do not rename the tool.

## Acceptance

- The declared build command succeeds.
- The header compiles alone in a translation unit of its own.
- A new `edit_file_lines_tests` target exists, compiles only this unit, and contains the scenarios currently misfiled elsewhere.
- It asserts: a replacement range replaces exactly those lines; an insertion at the start, middle and end lands at the documented index; a deletion of a range removes exactly those lines; an out-of-range request yields the documented error value; a range on a file whose line count changed is rejected as documented.
- The other target no longer asserts this unit's behaviour.
