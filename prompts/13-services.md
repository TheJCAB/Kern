# Fix: src/Services

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`, `07-endpoint.md`, `14-credentials.md`; if any has not landed, stop and say so.
Goal: split a file that is named for nothing into the three units it contains, and give config-path discovery a seam.

## What is wrong

1. The unit owns config-path discovery, config-file parsing and validation, and endpoint-plus-credential resolution — three concerns under a name that predicts none of them.
2. Config-path discovery calls the environment reader directly with no injection point, so its fallback order cannot be tested.
3. The environment-variable policy is duplicated between this unit and the runtime-paths unit, cross-referenced only by a comment.
4. The only production callers are two lines of the composition root.

## What to change

1. Split into three units by concern — config discovery, config parsing (keep the existing pure parse function and its tests), and service resolution — as a MOVE-ONLY change first, then behaviour changes separately; name each file for its concern.
2. Give discovery an injectable environment getter following the pattern already used for credential sources, and make it return a result that distinguishes "no config anywhere" from "config found but unreadable".
3. Point both this unit and the runtime-paths unit at one shared environment-variable helper, keeping the existing documented precedence order in one place.
4. Document in each new unit what it does not own.

## Non-goals

- Do not change the config JSON schema.
- Do not change the resolution precedence between built-in services and user config.
- Do not touch credential sources themselves.
- Do not add caching.

## Acceptance

- The declared build command succeeds.
- The old file no longer exists and no target lists it.
- `services_tests` is split or renamed so that each new unit's logic is exercised in its own target, and no target compiles a unit it does not test.
- Discovery with the environment variable present uses it.
- Discovery with the variable absent falls back in the documented order.
- Discovery with a present-but-unreadable file reports that distinctly from absent.
- The shared environment helper is used by both units: assert the behaviour, and state that the sharing itself is checked by reading the include list.
