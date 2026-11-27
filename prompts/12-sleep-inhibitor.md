# Fix: src/SleepInhibitor

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: document the one behaviour a reader can be harmed by — that the inhibitor silently degrades to a no-op — and give the unit a test that does not depend on the host having an inhibitor available.

## What is wrong

1. When no inhibitor is available the unit degrades silently to a no-op rather than reporting, and nothing in the header or the guard documents that.
2. Its untestability is inherent: it spawns a platform command or calls a platform API. That limit is written down nowhere, so a reader cannot tell "untested by necessity" from "untested by neglect".
3. It is used from exactly one place, so it is not a general utility and should not pretend to be.

## What to change

1. Document at the top of the header what the unit does, that it is best-effort, that a missing inhibitor is a silent no-op by design, and why that is acceptable: the alternative is aborting a session over a power-management preference.
2. Make the availability decision observable without spawning anything — expose a query for "would an inhibitor be used on this host", or record the last activation outcome as a value the caller can read — and document it.
3. State in the header that the guard's only caller is the composition root, so the unit is not a general-purpose utility.

## Non-goals

- Do not add a process-spawn seam; the review accepted the untestability as inherent.
- Do not change the platform commands.
- Do not change the destructor's escalation order.
- Do not add a second caller.

## Acceptance

- The declared build command succeeds.
- A new `sleep_inhibitor_tests` target exists and constructs and destroys the guard without requiring an inhibitor to be present.
- It asserts: construction never throws on a host with no inhibitor; the documented availability query agrees with whether the destructor had anything to tear down; destroying twice, or destroying an inactive guard, is harmless.
- The test carries a comment stating that the platform effect itself is unobservable in a unit test, and why.
