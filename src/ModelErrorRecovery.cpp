// OBSOLETE LEFTOVER — DELETE THIS FILE.
//
// This was created by mistake and duplicates src/ErrorRecovery.cpp, the translation unit that is
// actually built (see the `add_executable(kern` list in CMakeLists.txt and
// `error_recovery_tests` in tests/CMakeLists.txt). Nothing references it. It is kept empty on
// purpose only because the agent that created it had no way to delete it: it must be removed from
// the repository. Compiling it alongside src/ErrorRecovery.cpp would give duplicate definitions of
// `ErrorRecovery::OnProductiveTurn` and `ErrorRecovery::OnFailure`.
