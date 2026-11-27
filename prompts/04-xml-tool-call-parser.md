# Fix: src/XmlToolCallParser

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: make the tool-call XML parser a pure function with a documented, testable result type.

## What is wrong

1. The parser writes `std::println` debug lines, so a "string in, struct out" function nonetheless has terminal output.
2. `Parse` returns `nullopt` both when there are no more calls and when the input is malformed, conflating the two cases a caller must distinguish.
3. The two filter helpers in the header are unexplained.
4. The header states no contract for the `Parse` method beyond its name.

## What to change

1. Remove every `std::println` from the parser, or route them through an injected sink the caller can leave empty; the parsing behaviour must be identical either way.
2. Replace the `nullopt`-on-both-cases return with a result that distinguishes "no call present", "calls parsed" and "malformed", following the codebase's existing preference for a small struct or enum plus optional; keep the header-inline free function working by delegating to the new type.
3. Document both filter helpers and the class-level contract in a comment above each, stating what the filters match.
4. Document what the parser does with an unterminated tag, an unknown attribute and an empty body.

## Non-goals

- Do not change the XML grammar accepted.
- Do not move the file.
- Do not change `Session`'s call sites beyond adapting to the new result type.
- Do not add a logging dependency.

## Acceptance

- The declared build command succeeds.
- A NEW `xml_tool_call_parser_tests` target exists that compiles only `XmlToolCallParser.cpp` and its own dependencies, with no network, filesystem or terminal.
- A well-formed single call parses to one call with the expected name and argument map.
- Three calls in one document preserve order.
- An empty document reports "no call present", not an error.
- An unterminated tag reports malformed.
- An unknown attribute is retained or ignored as you documented.
- Parsing produces no stdout output when the sink is empty (assert by capturing or by constructing with a no-op sink).
- `Session`'s two call sites are updated to the new type.
