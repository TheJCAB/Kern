# "Kern" local agent harness

This workspace contains a minimal C++20 agent harness that can call a local llama.cpp server over REST and execute some simple tools:

- glob(pattern, root_dir)
- grep(pattern, file_path)
- read_file_chunk(path, start_line, end_line)
- edit_file_lines(path, operation, start_line, ...)
- write_file(path, content)

It also includes two specialized subagent tools: researcher and implementer. Each agent (the main orchestrator and the subagents) receives a subset of the tools as required by their mandate.

## Build

On Windows with VS 2026:

```cmd
cmake --preset MSVC-Release
cmake --build --preset MSVC-Release
```

On WSL Linux with clang and Ninja installed:

```bash
cmake --preset Clang-Release
cmake --build --preset Clang-Release
```

## Run

Start a llama.cpp server that exposes an OpenAI-compatible endpoint, for example:

```bash
llama-server -m /path/to/model.gguf --host 127.0.0.1 --port 8080
```

Note: on Windows you can use `winget llama.cpp` to install it.

Then run the harness:

```bash
./build/Release/kern --endpoint http://127.0.0.1:8080/v1/chat/completions --max-turns 10 "Analyze and review the src/main.cpp file. Be tough but fair"
```

The harness will send the request to the model, interpret tool calls, invoke the model with the tool results and then continue the loop until the number of turns is exhausted or the model declares the task complete.
