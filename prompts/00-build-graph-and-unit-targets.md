# Fix: all units

Status: unstarted.
Prerequisite: none
Goal: give every unit a target that compiles it alone, so a unit's dependencies stop being whatever the `kern` executable happens to include, and remove the files the review found dead.

## What is wrong

1. There is a single `add_executable(kern ...)` in the root `CMakeLists.txt` listing roughly 40 sources, so no unit is individually linkable and a test cannot link against a subset of the product.
2. Nineteen test executables each recompile whichever `../src/*.cpp` files they happen to need. `command_line_tests` compiles five sources to test one, and `runtime_paths_tests` compiles six for a two-function unit (see `tests/CMakeLists.txt`). The source list per test is therefore a copy of transitive includes, not a statement of what the test exercises.
3. Header-only units (`src/SubagentSessions.h`, the 11 `src/Tools/*.h`, `src/JsonUtilities.h`) are recompiled into every binary that includes them, so changing one constant rebuilds six test executables.
4. `src/Tools/subagent.h` is absent from the `kern` source list although two listed headers include it.
5. `src/ModelErrorRecovery.h` and `src/ModelErrorRecovery.cpp` are dead files whose first line says to delete them, and `src/SessionId.cpp` is a 15-line translation unit for one function.

## What to change

1. Add a `kern_core` target (static library, or object library if the owners prefer no archive) holding the current `src/*.cpp` source list. `kern` becomes `main.cpp` plus a link to `kern_core`, and `kern_core` carries the include directories and compile options the executable used to carry.
2. Add header-only `INTERFACE` targets for the tools and other header-only units that need include directories, so a consumer links the interface target instead of listing a `.h` file as a source. Keep the existing target names as aliases if any test refers to them.
3. Rewrite each `add_test`/`add_executable` in `tests/CMakeLists.txt` to link `kern_core` (or the narrower interface target) and drop the `../src/*.cpp` file list, EXCEPT where the point of the target is to isolate a unit with substituted dependencies, in which case keep compiling just that unit's implementation and document why in a comment above the target.
4. Remove the over-linked sources from `command_line_tests`, `runtime_paths_tests`, `tool_definition_tests`, `services_tests` and `file_validation_tests`, verifying each removal against the include graph rather than by luck: a source may only be dropped when nothing the test links still needs its symbols at link time.
5. Add `src/Tools/subagent.h` to the source list if it stays header-only in the target, or delete its entry if the interface target covers it. Either way no header stays in a source list that is not compiled.
6. Delete `src/ModelErrorRecovery.h` and `src/ModelErrorRecovery.cpp`. Do not delete `src/SessionId.cpp` here; that deletion belongs to prompt 08.

## Non-goals

- Do not change any C++ source logic, signatures or behaviour.
- Do not rename any test target or its CTest name; those names are referenced elsewhere.
- Do not restructure `data/` copying or the install rules.
- Do not add an external test framework or a new dependency.
- Do not alter `Kern.build.json` or the configure presets.
- Do not delete `src/SessionId.cpp` or any other file not named in item 6.

## Acceptance

- The declared build command succeeds from a clean `build/Kern-Test`.
- `kern` links `main.cpp` plus `kern_core`, and no object file appears twice on any link line.
- All 19 existing test names still exist in `ctest -N` and still run.
- After the change, the source list of each test target contains no `.cpp` that the test does not exercise; any target where that could not be achieved is listed in the prompt's result with the reason.
- `src/ModelErrorRecovery.h` and `src/ModelErrorRecovery.cpp` no longer exist and nothing in `src/`, `tests/` or `CMakeLists.txt` references them.
