# Fix: src/Credentials

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: close the tests' gaps on a unit whose seam is already right, and fix an include that inverts its stated dependency.

## What is wrong

1. The header includes the services header while the services header only forward-declares this one, so the dependency direction is stated differently in the two files.
2. The source-ordering of credential sources, the default source list, and the raw environment reader have no assertions, even though the getter seam is documented as replaceable for testing — the good seam is under-used rather than missing.
3. There is no dedicated test target; coverage is incidental.

## What to change

1. Remove the include of the services header from this header: forward-declare what is needed, or take a span of sources so no service type is required. Verify the direction is now one-way.
2. Document at the top of the header the resolution order across sources and what "not found" returns.
3. Add a dedicated target rather than adding to the incumbent one, so this unit's tests can be read in isolation.

## Non-goals

- Do not change the getter-based seam.
- Do not add sources.
- Do not change key sanitising behaviour.
- Do not touch how the endpoint receives a key.

## Acceptance

- The declared build command succeeds.
- A new `credentials_tests` target exists that compiles only this unit and its dependencies.
- Using the existing lambda-getter fake, the target asserts: a source that has a value wins and is sanitised; a source without a value is skipped without error; the documented precedence order is honoured with two sources both holding keys; no source holding a key returns the documented not-found result rather than an empty key; sanitising strips the documented characters at both ends.
- State that the raw environment reader stays untested by design and why, or test it via an injected getter if you can do so without touching the process environment.
