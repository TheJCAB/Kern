# Fix: src/JsonUtilities.h

Status: unstarted.
Prerequisite: `00-build-graph-and-unit-targets.md`; if it has not landed, stop and say so.
Goal: stop a global type alias from leaking into every header, and pin the null-versus-missing-versus-fallback contract that nothing currently states.

## What is wrong

1. The header declares a global using-alias for the JSON type, which leaks into every header that includes it, so the alias is part of dozens of contracts without any of them agreeing to it.
2. The header is 16 lines, declares a `std::string_view` parameter without including what it needs, and relies on transitively included declarations.
3. The get-or-default helper has roughly two dozen call sites and no test, so the difference between a key that is present-and-null, present-and-empty, and absent is undefined.

## What to change

1. Remove the global alias, put the alias inside the project's own namespace, and fix every include site to qualify it. This is a mechanical MOVE-ONLY change.
2. Add the missing standard include and any explicit project include the header truly needs, so that it compiles alone.
3. Document the helper's contract for all three cases above and for a wrong-typed value — throw, ignore or fall back — stating which the current code does. Make the documentation match the code, and change code only if the current behaviour is indefensible.
4. Give the header its own target so that it is compiled alone.

## Non-goals

- Do not wrap the JSON type in a class.
- Do not replace the JSON library.
- Do not change the wire format.
- Do not change call-site logic beyond qualification.

## Acceptance

- The declared build command succeeds.
- A new `json_utilities_tests` target exists whose only dependency is the JSON library.
- It asserts directly that an absent key yields the default, that a present key yields its value, that a present-but-null key yields the documented outcome, and that a wrong-typed key yields the documented outcome.
- A header-alone compilation check exists: a translation unit that includes only this header and is compiled in its own target, or an explicit note that the target achieves it.
- A grep confirms the global alias no longer exists unqualified at namespace scope; list the files you updated.
