#pragma once

#include <filesystem>

// Directory that holds the files the program reads at runtime, such as the system prompts. An installed program keeps them in `share/kern/data` beside its `bin` directory and a build tree keeps them in `data` beside the executable; which one applies is decided by looking at what is actually there, so no install prefix is ever compiled in. The directory is resolved once per process and is not created on demand.
std::filesystem::path GetRuntimeDataDirectory();

// Directory that receives the log files and session transcripts of a run that names no other directory. An installed program writes them to the per-user state directory, because the prefix it was installed into is usually not writable by the user running it: `%LOCALAPPDATA%\Kern\logs` on Windows, and `$XDG_STATE_HOME/Kern/logs` or `~/.local/state/Kern/logs` elsewhere. A build tree writes them to the `logs` directory beside the executable, where they sit next to the code they describe, and that directory is also the fallback when no per-user directory can be determined.
std::filesystem::path GetDefaultLogDirectory();
