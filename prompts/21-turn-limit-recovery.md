# Fix: src/TurnLimitRecovery

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md` and `20-recovery-policies.md`; if either has not landed, stop and say so.
Goal: re-scope a unit that is misnamed, so the ceiling it is supposed to govern is actually enforced where it lives.

## What is wrong

1. The unit is named for recovery but holds a turn-budget grant plus a shared error-code constant; it never enforces the ceiling, so `Session` must add the pending wrap-up turns itself — an undocumented, load-bearing caller contract.
2. The error-code constant it exports is consumed by two other files, so it is a shared vocabulary item wearing a policy unit's name and it will move with the unit needed to keep those two files compiling.
3. Its test target asserts constants only in places, including limits that belong to another unit, and prints a success line before its last assertions, so a later failure still reports success.

## What to change

1. Rename the unit to what it governs — a turn budget — as a move-only change: rename the header, the source and every include, and give the error-code constant a home in the shared vocabulary where its other two consumers already look.
2. Move the ceiling computation into the unit in a second, separate change, so the caller asks one question — "may this turn proceed, and what is the effective limit?" — and gets one answer struct carrying the verdict and the limit; delete the caller's arithmetic.
3. Document the wrap-up contract in the header: what a wrap-up turn may do, how many are granted, and what happens at the hard ceiling, including that the answer is stable for repeated asks in the same turn.
4. Move the assertions about another unit's constants into that unit's test target and move the success print to the last line of this unit's test.

## Non-goals

- Do not change the default grant or the wrap-up counts.
- Do not merge this unit with the retry policy unit that prompt 20 owns.
- Do not change `Session`'s loop structure beyond replacing the added arithmetic with the single new answer.
- Do not touch subagent turn limits; prompt 33 owns those.

## Acceptance

- The declared build command succeeds.
- The unit's file is renamed and no target lists the old name.
- The two call sites no longer perform ceiling arithmetic; verify this by reading them.
- The test target asserts that a turn under the limit may proceed with the expected remaining count, that the wrap-up turn is offered exactly once at the documented point, that at the hard ceiling further turns are refused with the named code, and that the caller cannot exceed the ceiling by asking twice.
- The test asserts no constant belonging to another unit, and its success line is last in the file.
