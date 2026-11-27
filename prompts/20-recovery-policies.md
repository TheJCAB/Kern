# Fix: ErrorRecovery + TokenLimitRecovery

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: collapse two near-identical retry policies into one parameterised unit, and make their guidance testable as structured decisions rather than prose.

## What is wrong

1. Two units share identical private state and a byte-identical productive-turn function; they differ only in the entry point, a retry constant and one extra assistant carry-over message, so the same policy is maintained twice.
2. Both print their guidance to the terminal, so the observable contract is prose and the tests assert substrings, which breaks on wording edits and hides real regressions.
3. One of them returns a reference to internal storage, which freezes the storage type into the public contract.

## What to change

1. Make one parameterised policy unit whose constructor or config carries the retry limit, the entry-point kind and whether an assistant carry-over message is emitted. Delete the second unit and route its caller to the merged one.
2. Return the guidance as a structured value — a small struct with a kind, a short stable code and the text — so that tests assert the code and the count, not the wording.
3. Return by value, or document why a reference is safe.
4. Document the escalation contract: what the caller sends, how many times, and what the caller must do when the retries are exhausted.

## Non-goals

- Do not change retry counts or the carry-over text.
- Do not add a clock; retries are counted, not timed, which the review confirms is correct.
- Do not merge the turn-budget unit into this one; prompt 21 owns it.
- Do not change `Session`'s recovery flow beyond adapting it to the merged type.

## Acceptance

- The declared build command succeeds.
- The second unit's file is gone and no target lists it.
- The existing two test targets are replaced by one, which asserts through the structured code that the first failure produces the initial guidance, that the retry count increments per failure, that the final failure reports exhausted, and that a productive turn resets the counter.
- The merged target asserts that the carry-over variant emits exactly one extra message where documented.
- No assertion in the merged target matches prose text.
