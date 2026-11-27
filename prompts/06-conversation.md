# Fix: src/Conversation

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: give the payload builder an honest name and remove the one comment that actively misleads.

## What is wrong

1. There is no `Conversation` type in the unit — it is one POD plus three free functions, while the conversation state itself lives in `Session`'s pimpl, so the name promises something the file does not contain.
2. A payload-options struct is documented as carrying whether the response is streamed, but has no such member, and the builder hard-codes streamed-on.
3. Two ordering rules are assumed and unstated: tool calls must precede their results, and a tool result's id must match an earlier assistant tool-call id.
4. A standard header is included twice.

## What to change

1. Rename the unit to what it is — a chat payload builder — renaming the files, the types and every reference, as a MOVE-ONLY change with no behaviour edits in the same commit.
2. Either add the documented streaming member and honour it, or delete the comment and document that the builder always requests a streamed response. Pick one and state which in the header.
3. Document both ordering rules above the builder's declaration, and where cheap, assert them; an out-of-order tool result is a programmer error, so prefer a defensive check that reports rather than silently emitting an invalid payload.
4. Remove the duplicate include.

## Non-goals

- Do not change the JSON payload shape other than the streaming flag you decide on.
- Do not move conversation state out of `Session`; that is prompt `01-session.md`.
- Do not add a class just to justify the old name.
- Do not reformat the file.

## Acceptance

- The declared build command succeeds.
- No reference to the old name remains; grep for it and list the files you checked.
- A NEW test target named after the new unit exists, compiles only that unit plus its dependencies, and asserts the following.
- A two-message conversation produces the expected message array in order.
- A conversation with a tool result whose id matches a prior assistant call produces the documented pairing.
- The streaming flag in the emitted payload equals the documented behaviour, asserted explicitly rather than by substring.
- An empty conversation produces a valid payload rather than throwing.
