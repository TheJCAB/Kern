# "Kern" local agent harness

This workspace contains a minimal C++20 agent harness that can call a local llama.cpp server over REST and execute some simple tools:

- glob(pattern, root_dir)
- grep(pattern, file_path)
- read_file_chunk(path, start_line, end_line)
- edit_file_lines(path, operation, start_line, ...)
- write_file(path, content)
- build()

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

Add `--session-tag TEXT` to label the run in its transcript, so that the run can be selected later:

```bash
./build/Release/kern --session-tag tool-schema-v3 "add a test"
```

## Letting Kern build your project

Kern has no shell and can never run an arbitrary command. The only way it can execute anything is an optional `build` tool, and that tool only exists if the owners of the workspace declare it in a `Kern.build.json` file at the root of the workspace:

```json
{
    "build": {
        "command": "cmake",
        "arguments": ["--build", "--preset", "Clang-Release"],
        "workingDirectory": ".",
        "timeoutSeconds": 600
    }
}
```

Playing safe with the tool surface is still not a sandbox: a `build` command runs the project's own tooling, and project files such as `CMakeLists.txt` are themselves writable, so a build can do whatever the project's build system can do.

A declaration may instead list several commands under `commands` when the build needs more than one step, for example configuring before building so that newly added source files or targets are picked up:

```json
{
    "build": {
        "commands": [
            { "command": "cmake", "arguments": ["--preset", "Clang-Release"] },
            { "command": "cmake", "arguments": ["--build", "--preset", "Clang-Release"] }
        ],
        "workingDirectory": ".",
        "timeoutSeconds": 600
    }
}
```

| Field | Required | Meaning |
|---|---|---|
| `command` | yes | Executable to run, looked up in `PATH`. It is started directly, never through a shell, so pipes, redirection and `&&` are not possible. |
| `commands` | no | Alternative to `command`: an array of command objects that run one after the other. Each object accepts the same `command` (required), `arguments`, `workingDirectory` and `timeoutSeconds` keys, and each has its own default timeout. |
| `arguments` | no | Array of strings passed to `command` verbatim. |
| `workingDirectory` | no | Directory to run in: relative to the workspace root, or absolute inside it. Defaults to the workspace root. |
| `timeoutSeconds` | no | Kills the build after this many seconds (1 to 3600, default 600). |

A declaration uses either a single `command` or a `commands` array, never both. `arguments`, `workingDirectory` and `timeoutSeconds` at the top level of `build` still apply to a single `command` declaration; inside a `commands` array each command object carries its own.

The tool is offered to both the `implement` and the `research` subagents when the workspace declares a build, but never to the orchestrating agent, and it takes no arguments at all: the model chooses *when* to build but never *what* to run, so nothing it writes can reach a command line. The commands of the declaration run in order, the build stops at the first command that fails, and the output of every command is combined into one report. The researcher can only use it to report pre-existing build errors, because it cannot edit files. The model gets back the combined stdout and stderr, the exit code and whether the build timed out; when a declaration lists several commands it also reports the exit code of the failing command and which step failed. Very long output is truncated to its first 8 KiB and its last 24 KiB.

`Kern.build.json` is read-only to Kern: every editing tool refuses to write to that path, because only the owners of a workspace may describe how to build it. Without the file there is no `build` tool at all; with an invalid one Kern refuses to start instead of quietly falling back to something else.

## What Kern may touch

Every path a tool receives is resolved before it is used, so a symbolic link is resolved to its target and a link that points outside the workspace is rejected. An agent therefore cannot read or write outside the workspace through a link, and a linked directory is never descended into by `glob` or `grep`, so a recursive search cannot silently leave the workspace. A symlinked file inside the workspace that points outside is still reported as a `glob` match, but reading it is refused.

`.git` is read-only to Kern, exactly like `Kern.build.json`, and the refusal covers everything below it, so a hook cannot be installed that runs the next time a human runs `git`. `Kern.build.json` is also covered by a prefix test, so no sub-path can be used to reach it, and on file systems that ignore case the comparison ignores case too.

The `build` directory and `.git` are hidden from `glob` and `grep` results because they are typically not relevant to a search. That list is navigation-only: it has no say in whether a write is allowed. `.git` is protected independently by the guarded file system, and `build` is not protected at all, because a workspace's generated output is ordinary content that Kern may write to.

The workspace instruction files, `Kern.txt` and `Kern.md`, are ordinary files that Kern may change. These guards are a safety net for mistakes rather than a security boundary against a build that the workspace owners themselves declared.

## Services and API keys

Every model server Kern talks to is a *service*: a name, an endpoint and, optionally, the place where its API key lives. Choose one each time you run Kern with `--service NAME`:

```bash
./build/Release/kern --service openai --model gpt-4.1 "Review src/main.cpp"
```

Two services are built in and need no configuration or key: `llama.cpp` (`http://127.0.0.1:8080/v1/chat/completions`, the default) and `ollama` (`http://127.0.0.1:11434/api/chat`). Use `--list-services` to see all services and whether their keys are set (key values are never shown). Use `--show-sessions` instead to see the recorded sessions: it prints a table of their id, tag, model and completion flag, then a count, and a manifest it cannot parse is listed as `unreadable` rather than skipped.

Other options: `--model NAME` overrides the service's default model, `--port N` overrides the endpoint's port, and `--endpoint URL` uses an ad-hoc endpoint that needs no key. `--record <on|off|auto>` controls session recording (`on` is the default, `auto` currently behaves as `on`, and `off` disables it entirely), `--session-dir PATH` writes the transcripts somewhere other than the `logs/` directory beside the executable, and `--session-tag TEXT` labels a run, for example `tool-schema-v3`.

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
| `maxTokens` | no | Caps the output of each response (sent as `max_tokens`). Some services reserve credits for the model's maximum output on every request, so a lower cap can avoid "insufficient balance" or rate-limit errors. Unset by default. |

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

### Session transcripts

Every run records itself, by default. Two files are written beside the executable's `logs/` directory (or under `--session-dir PATH`), both named with the run's log id: `<id>_session.json` is the manifest, written when the run starts and rewritten when it ends, and `<id>_turns.jsonl` gets one appended JSON line per turn. `--record off` disables this entirely.

The manifest holds `schemaVersion`, `complete`, `identity` (id, role, parent id, tag), `provenance` (build id, model, decoding parameters, endpoint host/port/path/secure, max tokens, max turns), `inputs` (system prompt, tool definitions) and `outcome`. Each turn line holds the turn number, the token usage, the wall time of the response, the finish reason, the assistant text, the tool calls made and their results, truncated to a first 2000-character excerpt (`resultTruncated` marks that).

- Recording is best effort: a transcript that cannot be written logs a warning, disables further recording for that run and never fails the run.
- A manifest with `"complete": false` marks a run that did not finish, so such a transcript is evidence rather than a case.
- Credential material is never written to a transcript; only the endpoint's host, port, path and secure flag are recorded.

## Planning

`planning/` holds proposals for work that is not finished: a plan may be partly implemented, but it lists only what is still pending, and nothing in it is documentation of the current behaviour of the code.

`planning/README.md` is the index: it lists the pending plans and the rules for managing that directory, so read it first. Each plan is a self-contained `NN-slug.md` file, opened by a `Status:` line and cross-referenced by relative links to the other plans.

- Source changes go in `src/`, tests in `tests/`, and pending design work that has been agreed but not built goes in `planning/` as a plan, not scattered through code comments or issues.
- Plans describe intent; the code and its tests remain the source of truth for what Kern actually does.
- A plan describes only pending work: implementing part of it rewrites it in the same change so that it matches the code as it now is, because the record of what was done belongs in the commit history rather than in the plan.
