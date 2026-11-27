# Fix: src/Tools/research.h and src/Tools/implement.h

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`, `24-tool-utilities.md`, `33-tools-subagent.md`; if any has not landed, stop and say so.
Goal: remove duplicated model-facing contract text from two near-clone tools, keeping them separate tools with separate roles.

## What is wrong

1. The two tools are structurally near-clones: identical optional parameters, near-identical session-resume prose, identical build-injection block, differing only in tool list, role and system-prompt file.
2. Roughly 30 lines of model-facing contract text are duplicated, so a wording fix must be made twice and will eventually be made once.
3. The build-injection rule is described identically in both files, and the "diagnostic only" restriction on the researcher's build tool is enforced only by that researcher's tool list lacking edit tools — nothing says so.

## What to change

1. Extract the shared optional-parameter tables (session id, turns, release) and the shared session-resume contract text into one place that both files use — a shared header beside them, or the tool-definition helper of the unit from prompt 24. Each tool file then holds only what differs: role, tool list, prompt file and description lead-in.
2. State the build-injection rule once, in the shared place, and reference it from both tools instead of repeating it.
3. Document that the researcher's diagnostic-only restriction is enforced by its tool list, and name the file that would have to change to break it, so a reader adding an edit tool there knows what invariant they are removing.
4. Change no wording the model reads: the shared text is the same text, moved.

## Non-goals

- Do not merge the two tools.
- Do not change parameter or tool names the model sees.
- Do not change the system-prompt files' contents.
- Do not change the session factory or release behaviour, which prompt 33 owns.

## Acceptance

- The declared build command succeeds.
- Grep shows the shared contract text exists exactly once and both tools reference it; the report quotes the grep.
- A new target, or additions to the existing schema target, asserts each tool's definition structurally: both expose the same three optional parameters with identical schema keys, each exposes its documented tool list, the researcher's list contains no edit tool, and the build entry appears only when build commands exist.
- Assert key sets and counts only, never prose wording.
- The test file states that the shared text is verified by reading rather than by assertion, because a prose assertion would be rewritten by the next wording fix.
