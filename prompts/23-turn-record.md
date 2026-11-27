# Fix: src/TurnRecord

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md` and `22-session-recorder.md`; if either has not landed, stop and say so.
Goal: replace two implicit contracts — a magic truncation number and a load-bearing setter order — with named, documented ones.

## What is wrong

1. The tool-result truncation limit is a literal inside the setter, with no named constant and no comment, so a reader cannot tell that results are truncated at all.
2. The correctness of the following-written text depends on the finish reason having been set first, which holds only by accident of the caller's call order.
3. The unit's test always builds a real recorder on a temp directory, so it re-tests its dependency as much as itself.

## What to change

1. Hoist the truncation limit to a named constant beside the other limits, with a comment stating that tool results are truncated for the transcript and that the value is a policy choice.
2. Remove the order dependence: write the following text only when the reason is present at flush time, whatever the setter order — or, if that is impossible, make the ordering an explicit documented precondition and check it defensively.
3. Document the unit's lifetime contract in the header: it writes on destruction, exactly once.
4. Make the test's dependency substitutable enough that the turn-record assertions can be read without reading the recorder's file format; if the recorder cannot be substituted, assert the record's contents through a recorder writing into a temp directory and say so in the test.

## Non-goals

- Do not change the transcript schema.
- Do not change truncation to a different number.
- Do not add a recorder interface; say instead in the report whether the remaining coupling is acceptable.
- Do not touch the recorder's own tests.

## Acceptance

- The declared build command succeeds.
- The truncation constant is named and used at the truncation site.
- A tool result longer than the limit is asserted to be truncated at exactly the limit.
- Setter order does not change the written result: assert that both orders produce identical output.
- A destroyed record writes exactly once, and the destructor write is asserted rather than assumed.
- Note in the report any assertion that still depends on the recorder's file format.
