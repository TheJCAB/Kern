# Fix: src/FileUtilities

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: split a three-concern unit into raw file IO, a path predicate and the glob engine, so the glob engine finally gets a target of its own.

## What is wrong

1. The unit holds raw read, write and chunk functions, the "is this path inside that root" containment predicate, and a large glob engine with private segment matchers — three concerns that never call each other.
2. The glob engine is the bulk of the file and is exercised only indirectly, so its edge cases — segment matching, case handling, ignore patterns — are effectively untested.
3. The containment predicate appears in no test file and is only exercised through a different unit.
4. The executable-directory helper falls back to the process current path and prints to stdout.

## What to change

1. Split into three units by concern as a MOVE-ONLY change first: raw file IO, path predicates, glob engine. Update every include and every build list.
2. Give the glob engine a dedicated target and assert its matching rules directly — segment matching, wildcard positions and the documented ignore behaviour — against literal expected result sets rather than filesystem sweeps wherever the matcher can be called on a string.
3. Assert the path predicate directly with a table including sibling-prefix traps, so that a root `/a/b` does not contain `/a/bc`, which is precisely the component-wise rule its comment claims.
4. Remove the stdout printing from the executable-directory helper and document its remaining fallback.

## Non-goals

- Do not change glob semantics or the options struct.
- Do not change file IO behaviour.
- Do not add a filesystem abstraction; integration tests on a temp directory remain acceptable for the IO unit, but say so in a comment.
- Do not change `FileValidation`; prompt 18 owns it.

## Acceptance

- The declared build command succeeds.
- The original file no longer exists.
- Each of the three new units has its own target, and each target compiles only that unit plus what it truly needs.
- Glob matcher assertions run with no filesystem at all.
- The path-predicate table includes the sibling-prefix trap and passes.
- No unit in the cluster prints to stdout.
- State which IO assertions still require a real temp directory, and why that is acceptable.
