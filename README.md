# "Kern" local agent harness

This workspace contains a minimal C++20 agent harness that can call a local llama.cpp server over REST and execute two simple tools:

- glob(pattern, root_dir)
- grep(pattern, file_path)
- read_file_chunk(path, start_line, end_line)
- edit_file_lines(path, operation, start_line, ...)
- write_file(path, content)

## Build

On WSL Linux with clang and Ninja installed:

```bash
cmake --preset clang-ninja
cmake --build --preset clang-ninja
```

### Build with vcpkg

If you have vcpkg installed and `VCPKG_ROOT` set, CMake will use the vcpkg toolchain automatically.

```bash
export VCPKG_ROOT="/path/to/vcpkg"
cmake --preset clang-ninja
cmake --build --preset clang-ninja
```

On Windows with PowerShell:

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset MSVC-Release
cmake --build --preset MSVC-Release
```

The project also falls back to CPM.cmake when vcpkg packages are not available.

> Note: the `vcpkg.json` manifest now includes a `builtin-baseline`, which is required for vcpkg manifest mode.

## Run

Start a llama.cpp server that exposes an OpenAI-compatible endpoint, for example:

```bash
./llama-server -m /path/to/gemma-4.gguf --host 127.0.0.1 --port 8080
```

Then run the harness:

```bash
./build/kern --endpoint http://127.0.0.1:8080/v1/chat/completions --max-turns 3 "Read README.md and summarise it"
```

The harness will send the request to the model, interpret a tool call such as `{"tool":"read_file","path":"README.md"}` and then continue the loop with the tool result.