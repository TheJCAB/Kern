# Brief 04 — Remediation plan

Answers: what do we do, in what order, with what acceptance test?
Read first: the protocol in `reviews/README.md`; the outputs of briefs 01, 02 and 03
Produces: an ordered, falsifiable work plan.

## Precondition

If you were not given all three earlier outputs, stop and say which one is missing. Do not reconstruct the missing pass from the tree. This brief discovers no findings; it orders the ones already written down. A finding that appears here for the first time is a defect in the plan, not a late discovery, so every item quotes its source rather than re-describing the code.

## Ordering rules

Order by unblocking power, not by severity. Apply these rules in sequence and justify each stage of the plan against the rule that placed it there:

1. Build-graph and unit-boundary work first, because until a unit can be compiled and linked on its own every later fix is unverifiable.
2. Deletions and merges next, while the inventory is fresh: a dead file or a duplicate policy removed costs nothing and shrinks everything that follows.
3. Pure-logic extractions before seam injections: a transformation lifted out of a lambda or a god-function becomes testable immediately and needs no interface.
4. Documentation of what is true after the above, never documentation of what was intended. A colophon written for code that is about to move is wasted work, so brief 03's documentation items land behind the moves.
5. Contract fixes last, when the code stops moving.
6. Never schedule a change whose acceptance test you cannot state in one sentence.

## What a work item must contain

Name every one of these on its own labelled line, nothing implied:

- the unit path, as it appears in the inventory;
- the one-sentence defect, quoted from the earlier output under its own `path:line` — not re-described;
- the change, in one sentence;
- what deliberately does not change;
- the acceptance test: a command or a specific assertion, plus its expected result;
- the blast radius: which targets, callers or files must change with it;
- the review pass that will re-check it: 01, 02 or 03.

An item without an acceptance test is a wish and must be cut.

## Sizing and sequencing

Break the plan into stages. Each stage leaves the tree compiling and every existing test target passing, and no stage relies on an item scheduled after it. Within a stage, order by blast radius, smallest first.

- Never mix a rename or a move with a behaviour change in one item, because a failing acceptance test then has two possible causes.
- Prefer many small items to few large ones; split any item whose blast radius you cannot list exhaustively.
- State the rollback for the one or two items that are genuinely hard to unwind, and say why they are hard.

## What to leave out

- No style or formatting work.
- No rewriting of the test framework.
- No build-system modernisation beyond what ordering rule 1 requires.
- No item that exists to satisfy a reviewer's preference.
- Nothing under `build/`.
- No item that revisits a verdict from brief 01. If a verdict is wrong, say so in a short `## Disagreements` section and stop rather than silently re-planning around it.

## Output shape

Copy this template literally.

```markdown
# Remediation plan — <commit sha or "working tree">

<One paragraph: N items across N stages; do <first item id> first because <the work it unblocks>.>

## Stages

1. <one-line goal> — items: <ids>
2. ...

## Items

### <id> — <short title>
- **Unit**: <path>
- **Finding**: "<quoted sentence>" — `<path:line>`
- **Change**: <one sentence>
- **Does not change**: <what stays>
- **Acceptance test**: <command or assertion> — expected: <result>
- **Blast radius**: <targets, callers, files>
- **Re-checked by**: <01 | 02 | 03>

## Disagreements

None.

## Not planned, and why

- <deferred thing> — deferred because <reason>.

## Credit

- <what an earlier review already solved well> — copy it rather than reinventing it.
```

## Definition of done

- [ ] Every scheduled item traces to a cited finding in an earlier output.
- [ ] Every item has an acceptance test and a blast radius.
- [ ] The first stage is build/boundary work, or carries a documented justification for not starting there.
- [ ] No item is scheduled whose acceptance test is unstateable.

## Worked example

```text
Example only, not items to carry forward.

### I1 — Extract history trimming from the send path
- Unit: `src/ChatClient.cpp` — Finding: "the trim block sits inside the network call and cannot be tested without a live endpoint" — `src/ChatClient.cpp:112-151`
- Change: lift the trim into a pure function over a span of messages and a budget; Does not change: wire format, retry policy, caller signatures
- Acceptance test: assert that trimming 10 messages down to a 3-message budget keeps the system prompt first and the last two messages in order — expected: both hold
- Blast radius: `src/ChatClient.cpp`, `src/ChatClient.h`, `tests/CMakeLists.txt` — Re-checked by: 02

### I2 — Delete the unused prompt loader
- Unit: `src/PromptLoader.h` — Finding: "nothing includes it and nothing should" (searched `PromptLoader.h`; DEAD in 01) — `src/PromptLoader.h:1`
- Change: delete the file and its line in `CMakeLists.txt`; Does not change: how `data/system-prompt.txt` is located at runtime
- Acceptance test: the tree builds and every existing test target passes — expected: exit 0
- Blast radius: `CMakeLists.txt` — Re-checked by: 01
```
