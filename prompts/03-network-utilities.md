# Fix: src/NetworkUtilities

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: extract the SSE frame parser from inside the HTTP call so the one piece of logic in this unit becomes testable, and give the two functions a stated contract.

## What is wrong

1. The SSE frame parser is written as a lambda inside the network call body, so it is unreachable without a live server even though it is pure.
2. `HttpPost` has no contract comment and its callback parameter is unnamed, so the undocumented facts — the callback fires once per complete event, before success is known, and the body may be replayed on retry — are unguessable.
3. The cleartext-key guard and IPv6 bracketing are duplicated between the GET and POST paths.
4. The logging globals are reached directly, so a test cannot silence or capture them.

## What to change

1. Extract the frame parser into a named, file-local or header-declared pure function taking the accumulated buffer and returning the complete events plus the unconsumed remainder; comment what constitutes a complete frame and what happens to a partial trailing frame.
2. Move the duplicated URL/key guard into one helper used by both verbs.
3. Name the callback parameter and document the three undocumented facts listed above directly above the declaration, including what the caller may do inside the callback and what the return value means if the callback ran.
4. State in the header that both functions reach the real network and name that as the seam a test would substitute.

## Non-goals

- Do not introduce an HTTP client abstraction or inject an `httplib` object — that belongs to prompt 02's sink seam.
- Do not add retries here.
- Do not change the wire format or headers sent.

## Acceptance

- The declared build command succeeds.
- A NEW `network_utilities_tests` target exists that compiles `NetworkUtilities.cpp` and asserts the extracted parser directly with no server and no network.
- A single complete event yields one event and an empty remainder.
- Two frames in one buffer yield two events in order.
- A partial trailing frame yields zero events and is returned as the remainder.
- A frame with a comment line or an empty data line is handled as documented.
- The parser test must not open a socket.
- State whether the `httplib` calls themselves remain untested and why that is acceptable.
