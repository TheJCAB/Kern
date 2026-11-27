# Fix: src/Endpoint

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: make the endpoint's two-phase construction explicit so its "parsed" invariant stops being false, and tighten one over-permissive host check.

## What is wrong

1. The parsed endpoint structure reads as fully populated, but the parser never sets the api key — another unit fills it later, so any "fully parsed" invariant is false and undocumented.
2. The loopback check accepts strings that are not loopback: it matches a bare `127.` prefix and something like `127.0.0.1.5`, so the documented `127.x.x.x` contract is looser than stated and those cases are untested.
3. The header has no unit-level statement of purpose, though its per-declaration comments are good.

## What to change

1. Document the two-phase construction directly above both the structure and the resolver, in one sentence each: what the parser fills, what another unit fills afterwards, and that the api key is absent until then.
2. Make the loopback predicate strict — a component-wise check of the four dotted decimal parts — and document the accepted forms: the named loopback host, the full range, and the bracketed IPv6 form if it is supported.
3. Add the unit-level colophon at the top of the header per the template in `reviews/03-documentation-and-caller-contracts.md` (prompt `03-network-utilities.md`), but do not add a doc-comment style the rest of the repo lacks; use plain comments directly above declarations.

## Non-goals

- Do not change the resolver's phase-2 caller.
- Do not add key handling to the parser.
- Do not change the endpoint's fields other than documenting them.
- Do not touch `Services`; that is prompt `13-services.md`.

## Acceptance

- The declared build command succeeds.
- The existing `services_tests` target still passes unmodified except for added assertions; add to it, or to a new `endpoint_tests` target if `services_tests` becomes over-full.
- Add assertions that the loopback predicate is true for the named loopback host and for `127.0.0.1`, `127.1.2.3`, and false for `127.` alone and for `127.0.0.1.5`.
- Add an assertion that a malformed endpoint reports the documented failure rather than a partially filled structure.
- Add an assertion that the parser leaves the key empty, and that this is now documented behaviour rather than an accident.
