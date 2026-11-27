# Fix: tests/

Status: unstarted.
Prerequisite: every source prompt, `00-build-graph-and-unit-targets.md` through `34-main-continue-parser.md`; if any of them is unstarted, stop and say so, because this prompt rewrites assertions whose expected text those prompts change.
Goal: make the test suite assert behaviour through structured outcomes instead of message wording, and stop it reporting success from a log that already printed a banner.

## What is wrong

1. FALSE GREENS: at least one test target prints "All tests passed!" and then keeps asserting for roughly twenty-five more lines, so a CI log shows success even when the later assertions fail; at least one assertion sits inside a `try` block whose `catch` is the only reachable failure path.
2. PROSE ASSERTIONS: about six suites assert substrings of human-readable messages rather than structured outcomes, so a wording edit breaks the test while the policy is unchanged, and a policy regression that preserves the wording passes. The affected areas are the two recovery policies, the turn budget, the note tool, the build-command error matching and the subagent runner.
3. CONSTANT-ONLY ASSERTIONS: some assertions check only that a constant equals a literal, including a block asserting another unit's constants — subagent lifetimes, turn limits, the live-session cap and error-code uniqueness — filed inside the turn-budget suite, which duplicates an equivalent block in the subagent suite.
4. SELF-REFERENTIAL ASSERTIONS: at least one suite recomputes its expectation by calling the function under test, so the test passes while both the expectation and the function are wrong.
5. DUPLICATION: the subagent constant block exists twice, and one suite re-tests its dependency's disable behaviour that the dependency's own suite already covers.
6. HARNESS GAPS: the shared test helper throws on the first failure, aborting the binary, and its header does not document that; there is no documented convention for how a test names its scenario; and style drifts between suites in println prefixes, namespace-comment spacing and use of `endl`.

## What to change

1. Move every success banner to the last executable statement of its test file. Audit every test file for a print that precedes an assertion, fix each one you find, and list the files you changed.
2. Replace every prose-substring assertion with a structured assertion. Where a source prompt introduced a structured result — the merged recovery policy's code, the tool envelope's error code, the parser's result kind — assert that code or kind. Where no structured value exists yet, either add one in the unit and say so, or assert the counts and flags instead of the message.
3. Delete the duplicated subagent constant block from the turn-budget suite and keep the copy in the subagent suite. Delete from any suite every assertion of another unit's constants, so that each suite asserts only what its own unit owns.
4. Replace self-referential expectations with literal expected values, so that a broken helper fails the test instead of defining it.
5. Document the harness in `tests/TestUtilities.h`: add one short paragraph at the top of the header describing the house style — one executable per unit, `main` returning non-zero on failure, each scenario named in one lowercase sentence and printed before its assertions — and a comment above each helper stating what it does and that `Expect` throws on first failure, so a failing binary reports only its first failure.
6. Normalise the drift: pick the convention that already dominates each of the three variation points — println prefix, namespace-comment spacing, `endl` versus newline — and apply that one, without reformatting anything else.

## Non-goals

- Do not adopt an external test framework; keep the hand-rolled executables and their `main`.
- Do not change what any test asserts about behaviour, only how it asserts it.
- Do not rename existing CTest target names; `prompts/README.md` and cross-references depend on them.
- Do not add tests for units that the source prompts cover.
- Do not reformat code outside `tests/`, and do not touch `build/`.

## Acceptance

- The declared build command succeeds, and every test target that existed before this prompt still exists and passes.
- A grep for a success banner preceding the last statement of a test file returns nothing.
- No assertion matches prose from a message that a source prompt made structured; list any prose assertion that remains with the reason it stays.
- The turn-budget suite asserts nothing belonging to another unit.
- The subagent constant block exists exactly once, in the subagent suite.
- No suite computes an expectation with the function under test; list any exception with its reason.
- `tests/TestUtilities.h` documents the abort-on-first-failure rule and the house test style.
- The final report states the count of assertions converted from prose to structured and the count of banners moved.
