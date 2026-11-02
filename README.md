# "Kern" local agent harness

This workspace contains a minimal C++20 agent harness that can call a local llama.cpp server over REST and execute some simple tools:

- glob(pattern, root_dir)
- grep(pattern, file_path)
- read_file_chunk(path, start_line, end_line)
- edit_file_lines(path, operation, start_line, ...)
- write_file(path, content)

It also includes two specialized subagent tools: researcher and implementer. Each agent (the main orchestrator and the subagents) receives a subset of the tools as required by their mandate.

## Build

Kern needs OpenSSL 3.0 or later (development files) so it can talk to HTTPS services.

- Debian/Ubuntu: `sudo apt install libssl-dev`
- Windows: install OpenSSL (for example `vcpkg install openssl`, or `winget install ShiningLight.OpenSSL.Dev`) and, if CMake can't find it, set `OPENSSL_ROOT_DIR`.

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
./build/Release/kern --service llama.cpp --max-turns 10 "Analyze and review the src/main.cpp file. Be tough but fair"
```

The harness will send the request to the model, interpret tool calls, invoke the model with the tool results and then continue the loop until the number of turns is exhausted or the model declares the task complete.

## Services and API keys

Every model server Kern talks to is a *service*: a name, an endpoint and, optionally, the place where its API key lives. Choose one each time you run Kern with `--service NAME`:

```bash
./build/Release/kern --service openai --model gpt-4.1 "Review src/main.cpp"
```

Two services are built in and need no configuration or key: `llama.cpp` (`http://127.0.0.1:8080/v1/chat/completions`, the default) and `ollama` (`http://127.0.0.1:11434/api/chat`). Use `--list-services` to see all services and whether their keys are set (key values are never shown).

Other options: `--model NAME` overrides the service's default model, `--port N` overrides the endpoint's port, and `--endpoint URL` uses an ad-hoc endpoint that needs no key.

### Declaring services

Add your own in `~/.config/Kern/services.json` (Linux; `$XDG_CONFIG_HOME/Kern/services.json` if set) or `%APPDATA%\Kern\services.json` (Windows). A service with the same name as a built-in replaces it.

```json
{
    "services": {
        "openai": {
            "endpoint": "https://api.openai.com/v1/chat/completions",
            "apiKeyEnv": "OPENAI_API_KEY",
            "model": "gpt-4.1"
        },
        "other-provider": {
            "endpoint": "https://api.example.com/v1/chat/completions",
            "apiKeyEnv": "EXAMPLE_API_KEY"
        },
        "lan-server": {
            "endpoint": "http://192.168.1.20:8080/v1/chat/completions",
            "allowInsecure": true
        }
    }
}
```

| Field | Required | Meaning |
|---|---|---|
| `endpoint` | yes | `http://` or `https://` URL of the chat completions API. |
| `apiKeyEnv` | no | Name of the environment variable holding this service's API key. |
| `model` | no | Default model for this service (the default is `gemma4`). |
| `allowInsecure` | no | Permits plain `http://` to a host that isn't this machine. |

### Providing the API key

Put each key in the environment variable named by its `apiKeyEnv`, in the shell you run Kern from:

```bash
export OPENAI_API_KEY='...'          # Linux (add to a private file such as ~/.profile only if you must)
```

```powershell
$env:OPENAI_API_KEY = '...'          # Windows PowerShell, current session only
setx OPENAI_API_KEY "..."            # Windows, persistent for the current user (new terminals only)
```

Kern never stores keys in `services.json` (it refuses to load a file with an `apiKey` field), and never prints or logs them. Each service has its own variable, so any number of services can be configured at once. If a service needs a key and the variable is missing or unusable, Kern stops before sending anything.

Tips: avoid typing keys directly on a command line that is saved in your shell history (for example, read it with `read -rs OPENAI_API_KEY; export OPENAI_API_KEY`), and never commit them to a repository.

### Transport security

- Keys are only sent over HTTPS (certificates are verified) or to this machine (`localhost`, `127.x.x.x`, `::1`).
- Plain `http://` to any other host is refused unless that service has `"allowInsecure": true` in `services.json`. Use this only for networks you trust, such as a server on your own LAN.

### Planned: OS credential store

Support for the Windows Credential Manager and libsecret is planned. It will be consulted before the environment variable, which will remain as a fallback, so existing setups will keep working unchanged.

## Troubleshooting and logging

Use `--log-level LEVEL` to get diagnostics on stderr (so they don't mix with the agent's output). Levels: `off`, `error`, `warning` (default), `info`, `debug`, `trace`.

```bash
./build/Release/kern --service openai --log-level debug "Review src/main.cpp"
```

- `error` / `warning`: failures, such as a connection or TLS error, or a response that ended without a finish reason.
- `debug`: each request (URL, size, model, message count), the HTTP status, timing, size of the response and the finish reason. It also saves the complete raw response of every turn as `logs/<id>_<turn>_response_raw.txt`, next to the `..._request.json` that is always written. The `logs` directory is next to the `kern` executable.
- `trace`: additionally prints every server-sent event as it arrives.

If the service answers with an HTTP error (wrong key, unknown model, rate limit...), Kern stops and shows the status and the start of the body. API keys are never logged: headers are never printed, and the key is also scrubbed from response bodies. The saved request and response files contain your conversation, so treat them as private.

