# Fix prompts

These are executable prompts, one per unit, that repair the defects found by the review briefs in `reviews/`. Each prompt is self-contained: an agent with file tools can run one without reading the others. `reviews/` analyses; `prompts/` changes code.

## How to run one

1. Run the declared build command first and record any pre-existing errors, because a prompt may not be blamed for them.
2. Give the agent one prompt at a time.
3. The agent must read the whole prompt before editing.
4. After the edits, the agent must run the build command itself and report the result.
5. If a prompt cannot be completed, the agent must say which acceptance bullet failed rather than partially applying edits and calling it done.

## The template

- `# Fix: <unit path(s)>` — names the unit or units the prompt owns.
- `Status:` — whether the prompt has been applied yet.
- `Prerequisite:` — which prompt must land first, or "none".
- `Goal:` — one sentence stating the end state.
- `## What is wrong` — numbered symptoms, each with evidence as `path:line` or a symbol name and what the code does today.
- `## What to change` — numbered items matching the list above, naming exact files, exact functions/types, and the shape of the new API.
- `## Non-goals` — concrete things the prompt must not touch, so the change stays reviewable.
- `## Acceptance` — independently checkable bullets; must include the declared build command succeeding, the named test target existing and compiling only the listed sources, and named specific assertions that must exist.

Two hard rules hold for every prompt: a prompt never mixes a file move or rename with a behaviour change, and a prompt never adds a test target that compiles sources it does not test.

## Order of work

1. `00-build-graph-and-unit-targets.md` must land first, because every other prompt's acceptance test depends on being able to compile a unit alone.
2. Then `03-network-utilities.md`, then `02-chat-transport.md`, then `01-session.md` — in that order, because the SSE parser extraction is consumed by the transport's sink seam, and the transport's seam is consumed by `Session`.
3. Then the remaining unit prompts in any order, subject to the pairwise dependencies stated in each prompt's `Prerequisite:` line.
4. `20-recovery-policies.md` before `21-turn-limit-recovery.md`.
5. `24-tool-utilities.md` before the `src/Tools/*` prompts.
6. `98-test-suite-hygiene.md` last, after all source prompts, because it rewrites assertions whose text the source prompts change.

## Index

| Prompt | Unit | Review verdict | Defect in one line |
| --- | --- | --- | --- |
| 00-build-graph-and-unit-targets.md | all units | n/a (cross-unit) | no library boundary; test targets over-link |
| 01-session.md | src/Session | JUSTIFIED (barely) | 250-line turn loop, no injection seam |
| 02-chat-transport.md | src/ChatTransport | MISNAMED | declared ctor never defined; real sleeps |
| 03-network-utilities.md | src/NetworkUtilities | THIN | SSE parser buried in a lambda |
| 04-xml-tool-call-parser.md | src/XmlToolCallParser | THIN | nullopt conflates empty and malformed |
| 05-model-response.md | src/ModelResponse | JUSTIFIED | dead non-streamed path; prints |
| 06-conversation.md | src/Conversation | MISNAMED | no Conversation type; false doc |
| 07-endpoint.md | src/Endpoint | JUSTIFIED | apiKey filled by another unit |
| 08-session-id.md | src/SessionId | THIN | entropy in a default initialiser |
| 09-log-file.md | src/LogFile | THIN | cached static directory; throws |
| 10-logging.md | src/Logging | THIN | mutable global, no sink |
| 11-run-process.md | src/RunProcess | JUSTIFIED | no seam; platform halves |
| 12-sleep-inhibitor.md | src/SleepInhibitor | JUSTIFIED | silent no-op undocumented |
| 13-services.md | src/Services | MISNAMED | three concerns in one name |
| 14-credentials.md | src/Credentials | JUSTIFIED | include inversion; gaps in tests |
| 15-runtime-paths.md | src/RuntimePaths | JUSTIFIED | once-per-process cache |
| 16-string-utilities.md | src/StringUtilities | THIN | console side effect with text helpers |
| 17-file-utilities.md | src/FileUtilities | SPLIT-NEEDED | raw IO + predicate + glob engine |
| 18-file-validation.md | src/FileValidation | JUSTIFIED | compile-time platform switch |
| 19-json-utilities.md | src/JsonUtilities.h | THIN | global using json; untested |
| 20-recovery-policies.md | ErrorRecovery + TokenLimitRecovery | DUPLICATE | one policy split in two |
| 21-turn-limit-recovery.md | src/TurnLimitRecovery | MISNAMED | ceiling not enforced here |
| 22-session-recorder.md | src/SessionRecorder | JUSTIFIED | record flag not honest |
| 23-turn-record.md | src/TurnRecord | JUSTIFIED | magic 2000; call order |
| 24-tool-utilities.md | src/ToolUtilities | SPLIT-NEEDED | four concerns; dispatch untested |
| 25-tools-grep.md | src/Tools/grep.h | JUSTIFIED | 425 lines, nothing testable |
| 26-tools-glob.md | src/Tools/glob.h | THIN | adapter; envelope duplicated |
| 27-tools-edit-file-lines.md | src/Tools/edit_file_lines.h | JUSTIFIED | missing standard includes |
| 28-tools-read-file-chunk.md | src/Tools/read_file_chunk.h | JUSTIFIED | wrong documented default |
| 29-tools-write-file.md | src/Tools/write_file.h | THIN | counts input not disk |
| 30-tools-delete-file.md | src/Tools/delete_file.h | JUSTIFIED | two error shapes, no code |
| 31-tools-build.md | src/Tools/build.h | JUSTIFIED | no process seam; no key table |
| 32-tools-research-and-implement.md | research + implement | THIN (dup) | ~30 lines duplicated |
| 33-tools-subagent.md | src/Tools/subagent.h | JUSTIFIED | prompt file not injectable |
| 34-main-continue-parser.md | src/main.cpp | composition root | /continue parsing untested |
| 98-test-suite-hygiene.md | tests/ | n/a (cross-unit) | prose assertions; false greens |

## Two prompts are merged on purpose

`20-recovery-policies.md` covers both `ErrorRecovery` and `TokenLimitRecovery` because the fix is to make them one parameterised unit; `32-tools-research-and-implement.md` covers both tools because the defect is duplicated contract text between them. Splitting either would create two prompts that must be applied together.

## What is not here

No prompt exists for `src/Tools/note.h` or `src/Tools/subagent.h`'s tool-definition layer, because the review found no defect in `note.h` (it is the one unit with a real isolating test) beyond the assertion style handled by prompt 98. Files end with a newline; nothing here may be written under `build/`.
