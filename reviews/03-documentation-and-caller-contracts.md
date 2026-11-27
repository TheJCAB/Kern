# Brief 03 — Documentation and caller contracts

Answers: can this unit be understood and trusted by its next reader?
Read first: reviews/README.md; the outputs of briefs 01 and 02
Produces: a false-comment audit, a caller-assumption register, and house documentation templates.

## Why this pass exists separately

Per-declaration prose is common in this codebase and sometimes excellent — `src/PromptCommand.h:28-35` states its parse rule more exactly than a test name ever would. But a unit cannot be documented "in isolation" if its file opens with includes and no statement of purpose, and it cannot be trusted if the prose that does exist contradicts the code. This pass measures the distance between what a unit says about itself and what it does. A reviewer who skips it reports style; a reviewer who runs it reports lies.

## The three documentation questions

### Unit-level colophon
Ask of every header, before the first `#include`: does it say what the unit is for, what it owns, its invariants, and what it assumes about its caller? None does today — every header under `src/` opens with `#pragma once`, a blank line and includes, e.g. `src/Session.h:1-13` and `src/Conversation.h:1-9`. There is no doc-comment convention in this repo at all; prose appears per declaration, or not at all. Do not prescribe a comment style here; the template is in section 6.

### Declaration contracts
For each public declaration, record whether the prose answers four things: what the caller must supply, what it gets back, which failures are values and which are exceptions, and any ordering or lifetime rule. `src/NetworkUtilities.h:19` is the model — failures arrive in the result except a request refused before sending, which throws. `src/Session.h:56` is the opposite: `Prompt` carries no prose, and `PromptResult::errorCode` at `:21` never names the values it can hold.

### Caller assumptions
This is the half that is almost always missing. Look for three shapes: a unit that requires its caller to do arithmetic it should do itself (a ceiling the caller must add, a pending-state the caller must query); a unit that requires its caller to call setters in a particular order for the output to be correct; and a unit whose contract depends on a second phase another unit performs (a field parsed here and filled there, which makes any "fully parsed" invariant false).

## Method

1. Read the header top to bottom before opening the implementation, so the first impression is the contract rather than the code.
2. List every public declaration and mark it documented, undocumented or contradicted.
3. Open the implementation and check each documented claim against the code. This step is the only way to find a false comment; skipping it produces a review of intentions.
4. Grep `src/` and `tests/` for the unit's callers and record every assumption the unit makes about them.
5. Write the caller-assumption register before writing any findings, so the findings describe a register rather than a hunch.
## What counts as a finding here

- **False comment** — the strongest finding; cite the comment line and the code line that disproves it.
- **Undocumented invariant** — a rule a test would have to encode because nothing else states it.
- **Contradicted default** — a documented default the code does not apply.
- **Name that hides the contract** — the declaration reads as one thing and does another.

- **Load-bearing, unstated caller assumption** — a caller can obey the header exactly and still break the unit.
- **Absent colophon** — no unit-level statement of purpose in a file large enough that a reader needs one.

Limits. Say nothing about comment density or formatting. Prose that is merely thin is not a finding unless a caller could act wrongly because of it. Open no more than three findings on any one unit.

## House documentation templates

Offer both in the output rather than applying them to source files: the colophon above a header's includes, the contract above each declaration.

```markdown
// <unit> — <purpose: one sentence, the verb this unit performs>
// Owns: <the state or policy this unit is the only home of>
// Does not own: <neighbouring concern, named so a reader does not look here>
// Invariants: <what is true between every pair of calls>
// Caller must: <what the caller has to do, in one clause>
// Test seam: <the dependency a test substitutes, or the word "none">
```

```markdown
// <name> — <one-line effect>
// Caller supplies: <arguments and their preconditions>
// Returns: <the value, and what the empty or zero case means>
// Failures as values: <which results report failure, and in which field>
// Failures as exceptions: <types, and the condition that throws each>
// Ordering: <calls that must precede this one, or "none">
// Lifetime: <what the caller must keep alive, or what this returns a reference into>
```

A unit whose colophon cannot be written without hedging is a unit whose boundary is wrong, and that observation belongs in the output as a finding, not as a hedge.

## Output shape

Copy this template literally.

```markdown
# Documentation and caller contracts — <commit sha or "working tree">

<One paragraph: which units were read at header level, the counts marked documented /
undocumented / contradicted, and how many caller assumptions were registered.>

## False-comment audit

| unit | comment | code that disproves it | consequence |
| --- | --- | --- | --- |
| `src/X.h` | `src/X.h:12` — <the claim> | `src/X.cpp:34` — <what the code does> | <who acts wrongly, and how> |

## Undocumented invariants

| unit | invariant | what a test must encode |
| --- | --- | --- |
| `src/X.h` | <the rule> | <the assertion> |

## Caller-assumption register

| assumption | stated where | held by | breaks if |
| --- | --- | --- | --- |
| <what the unit expects of its caller> | `path:line` or UNSTATED | `src/Caller.cpp:12` | <the change that breaks it> |

## Findings

### SEVERE
- **`src/X.h`** — <axis> — evidence `src/X.h:12` — <what is wrong in the code as written> — <what would make it right>.
### MODERATE
- ...
### MINOR
- ...

## Proposed colophons

- `src/X.h` — <the filled colophon template, no more than six lines>.

## Credit

- <unit> — <what is genuinely right and why> — `path:line`.

## Not verified

- <what> — <why it could not be checked>.
```

Credit is required and needs at least three items. This codebase writes some of its contracts for the model as a consumer, and writes them well, so credit there is expected rather than generous.

## Definition of done

- [ ] Every unit in the brief-01 inventory was read at header level and marked documented / undocumented / contradicted.
- [ ] Every false comment sits in the audit table with the line that disproves it.
- [ ] Every load-bearing, unstated caller assumption is in the register, with the caller that holds it.
- [ ] The colophon or a template was proposed for the units where it matters most, and no template was applied to a source file.

## Worked example

```text
Example only, not a finding to carry forward.

false-comment row: an options struct documented as carrying "whether the response is streamed".
| `src/Conversation.h` | `src/Conversation.h:27-28` — the options say whether the response is streamed
| `src/Conversation.cpp:77` — `ChatPayloadOptions` declares no such field; the body hard-codes "stream": true
| consequence: a caller that asks for a non-streamed answer gets an event stream and parses it as one body

caller-assumption row: a recovery ceiling the caller must add rather than the policy enforcing it.
| the session must add `PendingWrapUpTurns()` to its own turn ceiling before the report turn can run
| `src/TurnLimitRecovery.h:30-32` states that raise; held by `src/Session.cpp`, which owns the ceiling
| breaks if the session computes its ceiling once, before offering the wrap-up, and the report turn never runs
```
