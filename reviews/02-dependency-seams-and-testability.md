# Brief 02 — Dependency seams and testability

Answers: what does this unit need, and can it be tested?
Read first: reviews/README.md; the output of brief 01
Produces: dependency classes, test kind and test target per unit, plus findings.

## Why this pass exists separately

The untested set of this codebase is not random: it is close to the set of units that hide a dependency. Marking down "N units are untested" without naming the dependency that blocks each one is a complaint, not a review, because it tells the next agent nothing it can act on and quietly rewards whichever units happened to take their dependencies as parameters. This pass exists so that every missing test has a named cause and a named substitute, and so that a unit excused from testing is excused in writing rather than by omission.

## The absence rule (read before anything else)

Do not call a unit untested until you have done all three of the following, and record the search you ran:

- Search `tests/` for the unit's public symbol names, not its file name, and name the pattern you searched for.
- Read the unit's own `add_executable` block in `tests/CMakeLists.txt`, if one exists, and list the sources it compiles.
- Read every other test target's source list to see whether the unit is compiled in incidentally, and name the target that carries it.

Filename matching is forbidden as evidence. A unit whose test file has a different name is tested; a unit whose test file is named after it may still be untested, and a target named after it may compile none of it. Keep three lists as you go and report their counts: units with an isolating target, units compiled into some target incidentally, units in no target at all. The third list is the one that matters, and it is smaller than a filename diff suggests.

## Method

1. Per unit, list each dependency from the header and from the implementation, and classify it with the closed set in the protocol in `reviews/README.md`. Classify from the code, not from the comments: a comment that says a value is injected is not evidence that it is.
2. Identify the house seam pattern by reading what this codebase already does well, then use it as the yardstick for every other unit. Four seams are genuinely good: the abstract interface with a `Getter` lambda documented as replaceable for testing, `src/Credentials.h:36-39`; the runtime-context struct of substitutable `std::function` callbacks plus the filesystem facade, `src/ToolUtilities.h:73-89`; the `std::function` factory passed as a *call parameter* rather than stored in the constructor, `src/SubagentSessions.h:101` and `:107`; and a plain span or plain argument carrying the variable part, `src/FileUtilities.h:75` and `src/BuildCommand.h:54`. Name the file every time you cite the yardstick.
3. For each dependency that obstructs a test, write the substitute a test would use, in one clause: a fake implementing the interface, a `std::function` returning a canned value, a temp directory, a captured output sink, an injected clock, a fixed id. A finding that says "hard to test" without naming the substitute is incomplete and must not be written.
4. Assign a test kind per the protocol's closed set. When a target compiles the unit only to satisfy a link dependency of another unit, that is INCIDENTAL, not coverage, and you say which unit the target actually exercises.
5. Only then write findings, worst first, in the protocol's form.

## What to look for

1. **Real time in a test path.** Any `sleep`, `sleep_for`, timeout or poll loop reachable from a unit's logic, including one inside a retry helper. Cite the line, then state whether a clock seam exists, and say plainly that none does when none does.
2. **Output as contract.** Any unit whose observable behaviour is `std::print`/`std::println` or a stderr write rather than a returned value. This makes prose the contract and forces a test to match text; report it here even though the assertion-style evidence belongs to brief 03.
3. **Logic inside a lambda inside an I/O call.** Any pure transformation written inline in the body of a network, process or filesystem call. Cite the line range and note that extracting the transform is the whole fix.
4. **Process-wide state.** Cached statics, singletons, once-per-process path resolution, a global log level. State whether a second test in the same binary could observe a different value, because that is what makes a unit untestable in-process rather than merely awkward.
5. **Entropy in a default initialiser.** A random or time source reached without the caller asking, including in an aggregate's default member initialiser, which makes every construction of the type non-deterministic.
6. **Build-graph hygiene.** Per test target, compare the compiled source list against the unit actually exercised. Over-linking, a header-only unit recompiled into many binaries, and a source file in no target are all findings here, with the target name and line cited.

Limits. Write no fixes, rewrite no CMake, and propose no specific build restructure: that is brief 04. Open no more than three findings on any unit, and put anything beyond that into a one-line "also noted" list.

## Output shape

Copy this template literally.

```markdown
# Dependency seams and testability — <commit sha or "working tree">

<One paragraph: which units were classified, and the three counts: N units with an isolating target,
N compiled incidentally, N in no target at all. Name the source lists you compared.>

| unit | verdict | dependency classes | test target or NONE | test kind | one-line reason |
| --- | --- | --- | --- | --- | --- |
| `src/X.h` | <verdict from brief 01> | <classes> | `<target>` or NONE | <kind> | <reason> |

### Dependency inventory

| unit | dependency | class | substitute a test would use |
| --- | --- | --- | --- |
| `src/X.h` | <what it needs> | <class> | <one clause> |

## Findings

### SEVERE
- **`src/X.h`** — <axis> — evidence `src/X.h:12` — <what is wrong in the code as written> — <what would make it right>.

### MODERATE
- ...

### MINOR
- ...

## Test-target hygiene

- `<target>` (`tests/CMakeLists.txt:<line>`) — <what the source list compiles versus what it exercises> — <the gap>.

## Credit

- <unit> — <what is genuinely right and why> — `path:line`.

## Not verified

- <what> — <why it could not be checked>.
```

## Definition of done

- [ ] Every unit's every non-standard dependency carries a class and a named substitute.
- [ ] Every "untested" claim names the search pattern and the target lists you read.
- [ ] Every INCIDENTAL classification names the target that compiles the unit and the unit that target exercises.
- [ ] The reasonableness rule was applied rather than cited: any unit you excuse is excused in one written sentence naming the platform call it adds.

## Worked example

```text
Example only, not a finding to carry forward:
unit: src/SubagentSessions.h — abstract-interface seam, already tested.
inventory row: src/SubagentSessions.h | session factory | INJECTED | a hand-written fake
  implementing LiveSubagentSession, passed to Run(Factory const&, ...) at src/SubagentSessions.h:107.
Opposite case in the same family, one line of finding form:
  - **`src/Session.h`** — creation seam — evidence `src/Session.cpp:155` — a live session is built
    from a Config that reaches SessionRecorder and a log directory itself, so a test cannot hand
    it a canned session — pass the session factory in as a call parameter instead.
```
