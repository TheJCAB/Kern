# Fix: src/Tools/glob.h

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`, `24-tool-utilities.md`, `17-file-utilities.md`; if any has not landed, stop and say so.
Goal: keep this as the thin adapter that it is, and delete its copy of the result envelope.

## What is wrong

1. The unit is a thin argument-and-envelope adapter over the glob engine, and that is fine — but it duplicates the 13-key envelope and the failure lambda verbatim from the grep tool, so two copies must be kept in sync by hand.
2. It calls the glob engine directly rather than through the runtime context, so a test cannot substitute a fake tree.
3. Its own adapter behaviour — the truncated flag and the human summary — has no assertions, while the engine underneath it does.

## What to change

1. Delete the local envelope and the failure-lambda copy, and use the shared definition: ok/status/errorCode plus the stable named error-code vocabulary. That envelope's single definition lives with the dispatch unit from prompt 24 and is shared, not copied per tool.
2. Route the engine call through the context's filesystem facade, so a test can hand it a fake tree.
3. Document in the header that this unit is deliberately thin and that the matching semantics belong to the glob engine, naming that unit.
4. Keep the parameter schema unchanged.

## Non-goals

- Do not move matching logic into this unit.
- Do not change any semantic difference between this tool and the engine's defaults; document it instead if one exists.
- Do not add paging.
- Do not change the model-facing description text beyond correcting a factual error.

## Acceptance

- The declared build command succeeds.
- A new `glob_tool_tests` target exists that supplies a fake tree through the context and asserts: a pattern matching three files returns exactly those three in a stable order; zero matches returns the documented empty-but-ok result; a cap is reported with the documented truncated flag; an invalid pattern returns the named error code, not a throw; the summary is asserted only by its counts and flags, never as prose.
- Grep confirms the envelope is defined once in the codebase and that this unit holds no local copy of it.
