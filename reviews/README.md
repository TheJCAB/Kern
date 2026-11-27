# Reviews directory

This directory holds reusable review briefs. Each one is a self-contained prompt file you hand to an agent that has file-reading tools, and running one produces a review document, never code changes. A brief is not a plan, not documentation of current behaviour, and not a source of truth for it. Findings produced by a brief are not committed back into this directory.

They exist because a unit-level review of this codebase kept drifting into taste and architecture. These briefs fix the questions, the evidence rules and the output shape, so that two runs over the same commit are comparable and a reviewer cannot substitute suspicion for evidence.

## Briefs

| Brief | Question it answers | Run it when | Produces |
| --- | --- | --- | --- |
| [01-unit-inventory-and-justification.md](01-unit-inventory-and-justification.md) | Does this unit earn its place? | First pass over any commit, or after adding or removing a unit | One scoreboard row per unit plus findings |
| [02-dependency-seams-and-testability.md](02-dependency-seams-and-testability.md) | What does this unit need, and can it be tested? | Second pass, after the inventory | Dependency classes, test kinds, build-graph gaps, test-hygiene findings |
| [03-documentation-and-caller-contracts.md](03-documentation-and-caller-contracts.md) | Can this unit be understood and trusted by its next reader? | Third pass, after the dependency seams | False-comment audit, caller-assumption register, house documentation template |
| [04-remediation-plan.md](04-remediation-plan.md) | What do we do, in what order, with what acceptance test? | Only after 01 to 03 | An ordered, falsifiable work plan |
| [05-review-brief-retrospective.md](05-review-brief-retrospective.md) | Is this brief itself any good? | Whenever a brief is written or a review-specific subagent is created | A list of loopholes with rewritten clauses |

The three analysis passes are separate runs on purpose. Do not merge them, because merging makes a reviewer grade its own inventory.

## Review protocol

### What counts as a unit

A unit is one header plus, if present, its exactly-one implementation file: the smallest thing that has its own public contract. A unit is judged as a unit whether or not it is currently compiled into its own target. Tools under `src/Tools/` are units even though they are header-only. `main.cpp`, `tests/*` and build files are not units; `main.cpp` is a composition root and is judged only on whether it holds logic that belongs in a unit.

### Verdicts (closed set)

Use only these verdicts. Do not invent others, and do not write "mostly".

- **JUSTIFIED** — name predicts contents, one coherent concern, a real boundary, live in the build.
- **THIN** — the concern is real but too small or too scattered to be its own unit; it should be a header, or folded into a neighbour.
- **MISNAMED** — the unit is real, but its name does not predict its contents, so it cannot be documented or reviewed as written.
- **DEAD** — nothing includes it, or nothing should: a file that should be deleted.
- **DUPLICATE** — the same policy or contract exists elsewhere; one of the two should be a parameter of the other.
- **SPLIT-NEEDED** — two or more concerns that never interact share one name; the unit is several units.

### Dependency classes (closed set)

Use only these classes, in pass 02 and in the scoreboard.

- **INJECTED** — arrives through a constructor, a function parameter or a `std::function` member, and can be substituted.
- **PARAMETER** — a plain argument (path, config, span) passed in by the caller.
- **HIDDEN-GLOBAL** — a mutable singleton, a cached static, a global log level.
- **HIDDEN-IO** — filesystem, process, network, stdout/stderr, environment, or a system-prompt file read internally.
- **HIDDEN-NONDETERMINISM** — a clock, `sleep`, a random or entropy source, `__DATE__`/`__TIME__`.
- **HIDDEN-PLATFORM** — an `#if defined(...)` branch or an OS API, where one branch cannot run on the current host.

### What a finding is

A finding names one unit, one axis, one evidence location, what is wrong in the code as written, and in one clause what would make it right. Severity is SEVERE, MODERATE or MINOR.

- Every finding cites `path:line` and must be falsifiable in one step by opening that line.
- Report code, never intent. "The comment claims X, the code does Y" is a finding. "This seems intended to..." is not a finding.
- An absence claim ("untested", "unused", "not in the build") is only allowed if you ran the search and name the pattern you searched for.
- Never count style, formatting, performance or diff size. A name is only in scope when it makes the unit undocumentable, which is MISNAMED.
- Budget: at most three findings per unit, at most 60 words each. Anything further goes into a one-line "also noted" list.

### The reasonableness rule

"Reasonable" is where testability reviews go to die, so it is defined here. If the only obstacle to a unit test is a dependency that has not been injected — a clock, a process spawn, a filesystem path, an output sink, an entropy source, an environment variable, a cached static or a singleton — then the unit is unreasonable as written and that is a finding, not a defence. A unit is only excused from testing when the behaviour it adds is nothing beyond the platform call itself; say so explicitly when you excuse one.

### Scoreboard

Every pass that touches units produces a scoreboard with exactly these columns, one row per unit and one line per row: `unit | verdict | dependency classes | test target or NONE | test kind | one-line reason`. Test kinds are a closed set: **ISOLATING** (the target compiles only the unit and substitutes its dependencies), **INTEGRATION** (the real filesystem, process or network is the fixture), **INCIDENTAL** (exercised only inside another unit's target), **NONE**.

## Honesty rules

- Award credit. Every review ends with a short section naming what is genuinely right in the units reviewed and why, under the same evidence rules. A review with no credit section is incomplete, not tough.
- Prefer the smallest number of true findings to the largest number of plausible ones.
- If you could not verify something (a target's full source list, a platform branch, whether something compiles), say what you could not verify and why, in one line. Never infer it.
- Never state a fix as though it were applied. These briefs produce reviews.

## Using a brief

1. Give the agent the workspace and one brief at a time.
2. Tell it to read this README first: it holds the vocabulary, and the output must not restate or paraphrase it.
3. Run 01, then 02, then 03, keeping each output.
4. Run 04 only with all three outputs as input.
5. Keep the scoreboard format identical between passes, so a later run can be diffed against an earlier one.

## Managing this directory

- File names are `NN-kebab-case-slug.md`, with a two-digit number that only ever grows. Take the next free number and never renumber, insert or reuse one.
- Cross-references use relative markdown links to the file and source anchors as `path:line`. Never use an absolute path.
- Every file ends with a newline.
- Nothing here is generated by the build, and nothing may be written under `build/`.
- Briefs are edited in place as they improve. They carry no `Status:` line, because they are not plans.
- A brief must be usable on its own together with this README, and must not restate this protocol.
