# Fix: src/Tools/grep.h

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`, `24-tool-utilities.md`, `17-file-utilities.md`; if any has not landed, stop and say so.
Goal: make the largest tool unit contain something testable, and stop it bypassing the context seams the other tools use.

## What is wrong

1. The unit is 425 lines in which traversal, line matching, cap and truncation, histogram and reporting are fused into one function inside an anonymous namespace, so nothing is reachable from a test.
2. It calls the glob free function and opens files with a raw stream directly, bypassing the filesystem facade the runtime context already provides, so a test cannot substitute a fake tree.
3. Two genuinely pure helpers — the UTF-8-safe line truncation and the histogram key — are written as unexported file-local functions.
4. The file-cap termination condition is written out at three separate break sites, so the three copies can drift apart.
5. The result envelope is duplicated verbatim from the glob tool instead of being shared with it.

## What to change

1. Move the UTF-8-safe truncation and the histogram key into the shared tool-reporting unit or a small shared helper beside it, export them, and test them directly with a table that includes a multi-byte character straddling the cut.
2. Take the file search through the runtime context's filesystem facade, or through a documented injected root, instead of the raw glob call, so a fake tree can be supplied.
3. Collapse the three duplicated cap checks into one named predicate and call that predicate at each site.
4. Use the shared envelope — ok/status/errorCode plus the stable named error-code vocabulary — instead of a local copy of it. That envelope's single definition lives with the dispatch unit from prompt 24 and is shared, not copied per tool.
5. Keep the single-call shape and the existing parameter names, so the model-facing schema does not change.

## Non-goals

- Do not change the result schema's keys or the parameter schema.
- Do not change the match or ignore semantics.
- Do not replace the regex engine.
- Do not split the traversal into a library of its own.
- Do not change the histogram's buckets.

## Acceptance

- The declared build command succeeds.
- A new `grep_tool_tests` target exists and compiles only the grep unit and the shared helpers it now uses, not the whole tool cluster.
- With a fake tree supplied through the context, and no real filesystem where possible, it asserts: a matching pattern returns the expected file/line pairs in a stable order; a non-matching pattern returns the documented empty result with ok true; more results than the cap returns the cap and the documented truncated flag; a multi-byte character at the truncation boundary is not split; an invalid pattern yields the named error code rather than a thrown exception; binary or unreadable files are skipped as documented.
- The report names which of those assertions needed a real temporary directory and why.
