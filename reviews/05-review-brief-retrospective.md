# Brief 05 — Review brief retrospective

Answers: is this brief itself any good?
Read first: the protocol in reviews/README.md and the brief under review
Produces: a list of loopholes with rewritten clauses.

## Why this pass exists

A brief is a prompt, and a prompt has failure modes that a code review does not: it can be unanswerable, it can invite suspicion over evidence, it can be satisfied by volume, and it can reward the easiest interpretation. Those failures are invisible in the review output they produce — a run that obeyed a bad brief looks exactly like a run that obeyed a good one, because in both cases the document is complete, formatted and confident. So they need their own pass, run over the brief itself, before anything downstream is trusted.

## The loophole hunt

Read the brief under review as a prompt, not as prose. For each of the six failure modes below, find the clause that carries it, quote it, and write the replacement.

### Undefined terms

A word the brief leans on that no reader could apply identically twice — *unit*, *reasonable*, *cohesive*, *meaningful*, *adequate* — produces output that is well formed and not comparable between runs. Ask: could two competent reviewers disagree about whether this criterion is met, using the brief alone? If so, the term is undefined and the output is not comparable to the output of the last run, whatever it says.

### The reasonableness escape

Any clause that lets the reviewer excuse a unit instead of reporting it is the largest loophole in this directory, because it converts a finding into a judgement call about the author's intent. Ask: which sentence in this brief could be quoted to defend a hidden clock, a real sleep or a cached static? Quote that sentence verbatim, then rewrite it so the escape closes.

### Unfalsifiable findings

Output the brief cannot check is output that will be produced, because it costs nothing to write. Ask: does every required finding field include an evidence location, and does the brief forbid reporting intent? If a field can be filled in with a narrative instead of a `path:line`, the field is decorative and the brief needs to say so explicitly.

### Volume as success

Anything that makes a longer review look like a better one will be exploited, since the reviewer is an agent and effort is cheap. Ask: what in this brief rewards the number of findings, and what caps it? If there is no cap with a number in it, the review will pad to fit the template, and the padding will be indistinguishable from the findings.

### Filename and inference shortcuts

Any place the brief permits concluding from a name, a directory or a convention instead of a search produces absence claims that were never tested — "unused", "untested", "not in the build". Ask: does the brief force an absence claim to name its search? If it does not name the pattern, the claim is a guess wearing evidence's clothes.

### Self-graded inventory

A brief that asks one run to both decide a unit's fate and grade the decision will always grade itself generously, because the same pass authored both halves. Ask: does the pass separate judging a unit from testing or documenting it? If one document contains both, say which must be split out and into which pass.

## What a good brief contains

- A closed vocabulary, or a pointer to the one it uses, so a term has exactly one meaning per run.
- One question, stated as a question, so the reviewer knows what a finished answer looks like.
- An out-of-scope list, so the reviewer is not silently graded on what it was told to ignore.
- A method that says what evidence to collect at each step, not only what to conclude.
- A literal output template to copy, with the required fields visible in it.
- A definition of done whose items can each be marked or left unmarked without argument.
- A worked example that is real, short, and marked as an example so it is never carried forward as a finding.
- A cap on volume, expressed as a number, so length cannot stand in for quality.

## Rewrite, do not describe

- For every loophole you find, produce the replacement clause, quoted as it should read, not a description of what it should say.
- Write the rewrite in the brief's own register, so it can be pasted into the file at the cited line with no editing.
- Where a clause is unrepairable, say delete it and say what is lost, so the deletion is a decision and not an omission.
- Never return a list of concerns with no rewritten text, because that is the same failure the brief itself is being accused of.

## Output shape

Copy this template literally.

```markdown
# Brief retrospective — <brief file under review>

<One line: N loopholes found; verdict: runnable / runnable with the following rewrites / not runnable.>

## Loopholes

### <failure-mode name>
- **clause** — `reviews/<file>:<line>` — "<quoted clause>"
- **permits** — <what output this wrongly allows>
- **rewrite** — "<replacement clause as it should read>"

## Missing

- <what a good brief contains and this one does not>

## Keep

- <what the brief gets right and must not lose>

## Verdict

<No more than three sentences.>
```

The verdict line is a closed set of three values: `runnable`, `runnable with the following rewrites`, `not runnable`. Do not write any other value, and do not qualify these.

## Applying this to review subagents

When these briefs become the instructions of a dedicated review subagent, the subagent inherits the brief's loopholes at machine speed, so run this pass on the assembled instructions once, before trusting any output.

- Give the subagent one brief per run rather than the whole directory, because a subagent asked to do three passes grades its own inventory and runs out of turns mid-pass.
- Require the subagent's report to name the brief it ran and the pass, so a partial run is visible rather than plausible.

A subagent that stops mid-pass has kept its evidence, so resume it for the remainder rather than restarting, and never treat a long partial report as a completed one.

## Definition of done

- [ ] Every loophole has its clause quoted with `file:line`.
- [ ] Every loophole has rewritten text, not a description of one.
- [ ] The runnability verdict is one of the three closed values.
- [ ] The `## Keep` section is non-empty.

## Worked example

```text
Example only, not a loophole to carry forward:
loophole: Undefined terms / reasonableness escape
clause: "testable in isolation as far as is reasonable" — reviews/02:41
permits: a reviewer to excuse an uninjected clock as reasonable and report no
finding, so the same unit is THIN in one run and JUSTIFIED in the next.
rewrite: "testable in isolation, which means every dependency is a constructor
parameter, a function parameter or a std::function member; a clock, a sleep, a
spawn, a path or a cached static is a finding, not a reason to excuse the unit."
```
