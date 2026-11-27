# Fix: src/ToolUtilities (split into three units)

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md` and `20-recovery-policies.md`; if either has not landed, stop and say so.
Goal: split a four-concern "utilities" unit, and finally test the tool-dispatch path, which no test has ever exercised.

## What is wrong

1. The unit holds a tool-schema model and codec, prompt-fragment assembly, tool-call parsing plus dispatch, and the agent runtime context — four subjects that never interact.
2. Three test executables point at it, two of them covering prompt fragments and none covering dispatch, so the coverage looks triple what it is.
3. `ParseToolCalls`, the delta variant, `CallTool` and the argument parser appear in no test file at all, and neither does the unknown-tool or exception-to-error path.
4. The runtime context is the tools' inbound dependency bundle and belongs with dispatch, not with schema serialisation.
5. The file has no statement of purpose and opens with a stray blank line.

## What to change

1. Split into three units by concern — tool schema and codec, prompt-fragment assembly, and tool-call transport plus dispatch carrying the runtime context — as a move-only change first: update includes, the runtime context's home, and every tool header's include.
2. In a second change, fix the leading blank line and add a unit colophon to each new header per `reviews/03-documentation-and-caller-contracts.md`.
3. Keep the runtime context passed by const reference as today; the review found that pattern correct.
4. Document the dispatch contract in the dispatch header: what happens on an unknown tool name, a missing argument, an argument of the wrong type, and a tool that throws — each returned as a value, never as an exception escaping the dispatcher.
5. Replace the three overlapping test targets with three, one per new unit, where prompt fragments are compiled by exactly one target, and add a dispatch target that compiles the dispatch unit alone against a fake tool.

## Non-goals

- Do not change any tool's behaviour or schema.
- Do not change the tool result envelope; prompts 25-32 own it.
- Do not adopt a reflection-based schema generator.
- Do not move the tool headers themselves.

## Acceptance

- The declared build command succeeds.
- Three targets replace the current arrangement, and prompt fragments are covered by exactly one target.
- The new dispatch target compiles the dispatch unit alone and asserts, with a fake tool callable: a known tool name invokes it with the parsed arguments; an unknown name yields the documented error value and never throws; a missing required argument yields the argument-error value; a wrong-typed argument yields the same; a tool that throws is converted to the documented error value with the message preserved; the two parse overloads are asserted against literal call shapes.
- The report lists the tests you deleted or merged and why.
