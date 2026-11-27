# Fix: src/FileValidation

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md` and `17-file-utilities.md`; if either has not landed, stop and say so.
Goal: make the platform-dependent half of this security-relevant predicate reachable in tests instead of dead on one platform.

## What is wrong

1. Case-insensitive path comparison is selected at compile time, so on a case-sensitive host that entire branch is dead code and untested — in the unit that decides what may be written.
2. The unit links the glob and file unit only for the containment predicate, so the unit boundary and the test boundary disagree.
3. The validation policy itself, with injected roots and protected paths, is the cleanest dependency story in the cluster and is under-asserted relative to its importance.

## What to change

1. Make path-case folding a runtime property of the validator — a documented constructor parameter or a platform-preset default — rather than a compile-time branch, so that both behaviours are reachable in one test binary.
2. Document precisely what is refused and in what precedence: unknown root, protected path, missing file, and the write variant's rules.
3. Add assertions for the refusal precedence order, because a caller reporting the wrong reason is a real failure mode.

## Non-goals

- Do not change the list of protected names or the version-control directory rule.
- Do not weaken any refusal.
- Do not add a doc-comment style the repo lacks.
- Do not change the builder that registers roots; prompt 34 covers the composition-root policy.

## Acceptance

- The declared build command succeeds.
- No `#if` guards the fold decision: read the file and confirm the behaviour is no longer selected at compile time.
- Assertions cover, on one host, that an unknown root is refused with its own reason, that a protected path inside a known root is refused with its own reason and that the protected reason wins over a generic one, and that a path outside every root is refused with the outside reason.
- An assertion covers that the write variant refuses a missing parent or file per its documented rule.
- An assertion constructs a case-insensitive validator explicitly and shows that a differently-cased path inside a root is accepted.
