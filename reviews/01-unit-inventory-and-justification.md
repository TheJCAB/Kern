# Brief 01 — Unit inventory and justification

Answers: does this unit earn its place?
Read first: reviews/README.md (protocol and vocabulary)
Produces: one scoreboard row per unit, plus findings.

## Why this pass exists separately

A unit review that starts from tests, or from documentation, ends up grading the units that are easiest to test or easiest to read, and then presents that shortlist as if it were a coverage map. This pass settles the inventory first, so the later passes work from an agreed list with agreed verdicts. Skip it and every later finding is about a unit nobody ever decided should exist.

## Scope

Enumerate every unit using the protocol's definition in `reviews/README.md`: every header under `src/` and `src/Tools/`, each with its implementation file if one exists. Take the inventory from the filesystem, not from the build file.

Then compare that filesystem inventory against `CMakeLists.txt`. The difference between the two is itself a finding: a unit present on disk but absent from the build, or an implementation file compiled into no target.

Record this in one line, because it shapes every dependency answer in this pass: the project has a single `add_executable(kern ...)` covering all of `src/`, so every unit's dependency set is presently whatever that one target happens to include.

Out of scope for this pass: `main.cpp` beyond the single question of whether it holds logic that belongs in a unit; `tests/`; `data/`; `CMakeLists.txt` as a file to review (you use it only as a reconciliation list); and anything under `build/`.

## Method

1. List the headers, locate each implementation file, then reconcile against `CMakeLists.txt`. Produce three named lists: headers on disk, implementation files on disk, and what the build file actually compiles.
2. For each unit, read the header in full and skim the implementation. Record the public exports with a count rather than a listing, the verb the unit's name promises, and the concern the code actually performs.
3. Decide the verdict from the protocol's closed set. When two verdicts fit, prefer the lower-severity one and justify a THIN as THIN rather than as DEAD.
4. Write one scoreboard row per unit, using the protocol's exact columns. Populate the "test target or NONE" column only from the absence-verification rule in brief 02 if you have actually run it; otherwise write `not verified in this pass`. Never guess it.
5. Write findings only after every row exists.

## What to look for, and what the finding should say

1. **Name predicts contents.** Compare the type or function the header promises against the types it actually declares. If a file named after a type does not declare that type, the unit is MISNAMED and that is a finding citing the header line.
2. **Declared but never defined / never used.** Look for a declaration in the header with no definition and no caller: search the tree for the symbol name and record the pattern you searched. A declared member that cannot link is a finding regardless of whether anything constructs it today.
3. **God-object boundary.** Count the distinct stages inside the largest function of the unit. State the count and the line range; the count is the evidence, and "too long" without a count is not a finding.
4. **Bucket names.** Flag any unit named for a container (`Utilities`, `Helpers`, `Services`, `Management`, `Common`) and name the unrelated concerns it holds, with the line range of each. Do not propose the split here; the split belongs to brief 04.
5. **Duplication across units.** Compare the public surface, then the bodies, of units in the same family. Two units that differ only in an entry point, a constant and one extra transform are DUPLICATE: cite both files and the identical lines.
6. **Composition root.** For `main.cpp`, name each block of logic that has a pure, testable shape and is nonetheless inline, with its line range, and classify the remaining blocks as composition.

Hard limits. Do not propose refactors, migrations or file moves in this pass. Do not judge style, naming conventions beyond the MISNAMED test, performance, or diff size. Do not open more than three findings on any one unit; anything beyond that goes into the "also noted" list.

## Output shape

Copy this template literally.

```markdown
# Unit inventory and justification — <commit sha or "working tree">

<One paragraph: what was inventoried, that the list came from the filesystem and was then reconciled
against CMakeLists.txt, and the counts: N headers, N implementation files, N header-only units.>

| unit | verdict | dependency classes | test target or NONE | test kind | one-line reason |
| --- | --- | --- | --- | --- | --- |
| `src/X.h` | <verdict> | <classes> | `not verified in this pass` | <kind> | <reason> |

## Findings

### SEVERE
- **`src/X.h`** — <axis> — evidence `src/X.h:12` — <what is wrong in the code as written> — <what would make it right>.

### MODERATE
- ...

### MINOR
- ...

## Also noted

- <one line per observation that exceeded the per-unit finding budget>

## Credit

- <unit> — <what is genuinely right and why> — `path:line`.

## Not verified

- <what> — <why it could not be checked>.
```

## Definition of done

- [ ] Every header on disk appears in exactly one scoreboard row.
- [ ] Every verdict comes from the protocol's closed set.
- [ ] Every MISNAMED, DEAD or DUPLICATE verdict cites the header line or the caller search that proves it, and names the searched pattern.
- [ ] The credit section and the not-verified section are both present, and each is either non-empty or explicitly declared empty.

## Worked example

```text
Example only, not a finding to carry forward:
unit: src/ChatTransport.h — MISNAMED
src/ChatTransport.h:14-15 declares class ChatTransport; :24 declares a constructor.
Search "ChatTransport(" finds no definition; the sole caller uses only the static,
src/ChatTransport.h:28. The unit is real, the name is not what it exports.
```
