#include "BuildCommand.h"
#include "CommandLine.h"
#include "Credentials.h"
#include "FileUtilities.h"
#include "JsonUtilities.h"
#include "Logging.h"
#include "NetworkUtilities.h"
#include "PromptCommand.h"
#include "RuntimePaths.h"
#include "Services.h"
#include "Session.h"
#include "SessionRecorder.h"
#include "SleepInhibitor.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"
#include "TurnLimitRecovery.h"

#include "Tools/delete_file.h"
#include "Tools/implement.h"
#include "Tools/note.h"
#include "Tools/read_file_chunk.h"
#include "Tools/research.h"
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

// These defaults are intentional: Kern is meant to be used against a local server.
constexpr char DefaultServiceName[]  = "llama.cpp";
constexpr std::string_view WorkspaceInstructionFiles[] =
{
    "Kern.txt",
    "Kern.md",
};

constexpr ToolDefinition MainTools[] =
{
    Tools::research::Definition,
    Tools::implement::Definition,
    read_file_chunk,
    note,
    delete_file,
};

// Everything a session needs that comes from the command line and the workspace,
// as opposed to the per-session system prompt and tool list.
struct SessionSettings
{
    Endpoint    endpoint;
    std::string model;
    // Name of the service `model` is served by; empty when the endpoint was given ad hoc.
    std::string serviceName;
    std::string commonSystemPrompt;
    std::string workspaceInstructions;
    unsigned    maxTokens = 0; // 0 = no limit
    unsigned    maxTurns = 0; // 0 = no limit
    // Whether each session writes a transcript; --record off disables it.
    bool record = true;
    // Where transcripts go; empty means GetDefaultLogDirectory(), the per-user state directory when installed.
    std::filesystem::path sessionDirectory{};
    // Free-form label handed to every session identity.
    std::string sessionTag{};

    // Empty unless the owners of the workspace declared how to build it.
    std::optional<BuildCommands> buildCommands;
};

// Owns the real session of one live subagent, so the registry can hold it and resume it without ever knowing
// what a Session is. The session stays inside this object for as long as the subagent is live, which is what
// keeps a stopped subagent's conversation, its record and its transcript available for the next call.
class SessionSubagent final : public LiveSubagentSession
{
public:
    explicit SessionSubagent(Session session) : m_session{ std::move(session) } {}

    [[nodiscard]] std::string const& Id() const override { return m_session.Id(); }

    SubagentTurnOutcome Run(std::string_view prompt) override
    {
        Session::PromptResult const result = m_session.Prompt(prompt);
        return SubagentTurnOutcome{
            .ok                 = result.ok,
            .errorCode          = result.errorCode,
            .content            = result.content,
            .turnsUsed          = result.turnsUsed,
            .stoppedAtTurnLimit = result.stoppedAtTurnLimit,
        };
    }

    void GrantTurns(unsigned extraTurns) override { m_session.GrantTurns(extraTurns); }

private:
    Session m_session;
};

// Releases every live subagent session however the main session ends, because nothing else in the process owns
// them: each one holds a conversation that never shrinks until it is released.
class SubagentReleaser
{
public:
    explicit SubagentReleaser(std::function<ReleaseSubagentSessionsFunction> release) : m_release{ std::move(release) } {}

    SubagentReleaser(SubagentReleaser const&)            = delete;
    SubagentReleaser& operator=(SubagentReleaser const&) = delete;

    ~SubagentReleaser()
    {
        if (m_release)
        {
            // An empty id releases all of them.
            m_release(std::string_view{});
        }
    }

private:
    std::function<ReleaseSubagentSessionsFunction> m_release;
};

std::string LoadWorkspaceInstructions()
{
    std::string instructions;

    for (std::string_view const fileName : WorkspaceInstructionFiles)
    {
        std::error_code error;
        if (!std::filesystem::exists(fileName, error))
        {
            continue;
        }

        // These are appended to the system prompt, so a separator line is added unconditionally.
        instructions += '\n';
        instructions += RawReadTextFile(fileName);
    }

    return instructions;
}

SessionSettings ResolveSessionSettings(CommandLineOptions const& options)
{
    SessionSettings settings;

    if (!options.endpointUrl.empty())
    {
        // Ad-hoc endpoints have no API key, so there's nothing to protect.
        settings.endpoint = ParseEndpoint(options.endpointUrl);
    }
    else
    {
        std::vector<ServiceDefinition> const services = LoadServices();
        ServiceDefinition const& service = FindService(services, options.serviceName.empty() ? DefaultServiceName : options.serviceName);
        settings.endpoint = ResolveService(service, DefaultCredentialSources());
        settings.model    = service.model;
        settings.serviceName = service.name;
        settings.maxTokens = service.maxTokens;
    }

    if (options.model)
    {
        settings.model = *options.model;
    }
    if (options.port)
    {
        settings.endpoint.port = *options.port;
    }

    settings.record           = options.record;
    settings.sessionDirectory = options.sessionDirectory;
    settings.sessionTag       = options.sessionTag;
    settings.maxTurns         = options.maxTurns;

    settings.commonSystemPrompt = RawReadTextFile(GetRuntimeDataDirectory() / "CommonSystemPrompt.txt");
    settings.workspaceInstructions = LoadWorkspaceInstructions();

    // A workspace that declares no build simply has no build tool at all, while one that
    // declares an invalid build is fatal: it must never be silently ignored.
    settings.buildCommands = LoadBuildConfiguration(std::filesystem::current_path());

    return settings;
}

// Builds the context that tools use. The factory captures the settings by value, so no session ever refers
// back to this function's locals; the live subagent sessions live in a registry that the factory shares by
// value and that the main session releases when it ends.
ToolsRuntimeContext MakeToolsRuntimeContext(SessionSettings settings)
{
    // Read out before `settings` is moved into the factory lambda below.
    std::optional<BuildCommands> const buildCommands = settings.buildCommands;

    ValidatedFileSystem fs{ std::filesystem::current_path() };

    // Kern may read the build declaration but the tool list only reads it, and the guarded file
    // system must refuse any write to it: only the owners of the workspace may change it.
    fs.AddProtectedPath(std::filesystem::current_path() / BuildConfigurationFileName);

    // A file written into version control metadata is a command that runs the next time a human runs
    // git, so the guarded file system must refuse every write below it.
    fs.AddProtectedPath(std::filesystem::current_path() / VersionControlDirectoryName);

    // The live subagent sessions of this main session. Both lambdas below share it through a std::shared_ptr, so
    // the factory stays a value capture and no session ever refers back to a local of this function.
    auto const subagents = std::make_shared<SubagentSessions>();

    return ToolsRuntimeContext{
        .createNewSession = [settings = std::move(settings), subagents](
            ToolsRuntimeContext const&      context,
            std::string_view                parentSessionId,
            std::string_view                role,
            std::string_view                systemPrompt,
            std::span<ToolDefinition const> tools,
            unsigned                        maxTurns,
            std::optional<std::string_view> resumeSessionId,
            std::string_view                prompt
        ) -> std::expected<SubagentRunResult, SubagentError>
        {
            // Only a call that starts a new subagent reaches this: a resume reuses the session the registry already
            // holds, so it keeps the very same history, turn counter and record instead of starting a new one.
            auto const makeSession = [&](unsigned const budget) -> std::unique_ptr<LiveSubagentSession>
            {
                return std::make_unique<SessionSubagent>(Session{Session::Config{
                    .endpointDescriptor = settings.endpoint,
                    .toolContext        = context,
                    .systemPrompt       = std::string(systemPrompt) + settings.commonSystemPrompt + BuildToolPromptFragments(tools)
                                          + settings.workspaceInstructions,
                    .modelName          = settings.model,
                    .tools              = tools,
                    .maxTokens          = settings.maxTokens,
                    .maxTurns           = budget,
                    .identity           = SessionIdentity{ .parentId{ parentSessionId }, .role{ role }, .tag = settings.sessionTag },
                    .sessionDirectory   = settings.sessionDirectory,
                    .record             = settings.record,
                }});
            };

            return subagents->Run(makeSession, parentSessionId, maxTurns, resumeSessionId, prompt, context.turnEpoch);
        },
        .releaseSubagentSessions = [subagents](std::string_view sessionId) -> std::size_t
        {
            // A tool releases one session by id; an empty id releases all of them, which is what the end of the main
            // session asks for.
            if (sessionId.empty())
            {
                std::size_t const live = subagents->LiveCount();
                subagents->ReleaseAll();
                return live;
            }

            return subagents->Release(sessionId) ? 1u : 0u;
        },
        .fs            = std::move(fs),
        .buildCommands = buildCommands,
        // This list is navigation-only: it hides these directories from glob and grep results
        // because they are typically not relevant. It deliberately has no say in whether a write is
        // allowed: ".git" is protected by the guarded file system, and "build" is not protected at
        // all.
        .ignoredDirectories = { ".git", "build" },
        .turnEpoch          = 0,
    };
}


int ListServices()
{
    auto const sources = DefaultCredentialSources();
    for (ServiceDefinition const& service : LoadServices())
    {
        std::string keyStatus = "no API key";
        if (!service.apiKeyEnv.empty())
        {
            // Only report whether a key is available; never its value.
            keyStatus = std::format("{} ({})", service.apiKeyEnv, FindApiKey(service, sources) ? "set" : "NOT SET");
        }
        std::println("{:<16} {}  [{}]", service.name, service.endpoint, keyStatus);
    }
    return 0;
}

// Prints one line per recorded session, newest id first, then a count. Reads only
// the manifests written by SessionRecorder; a manifest that will not parse is
// reported as unreadable rather than skipped silently.
int ShowSessions(CommandLineOptions const& options)
{
    std::filesystem::path const directory = options.sessionDirectory.empty() ? GetDefaultLogDirectory() : options.sessionDirectory;

    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory))
    {
        std::print("no recorded sessions in {}\n", directory.string());
        return 0;
    }

    struct RecordedSession
    {
        std::string id;
        std::string tag;
        std::string model;
        std::string status;
    };

    std::vector<RecordedSession> sessions;

    try
    {
        constexpr std::string_view suffix = "_session.json";

        for (std::filesystem::directory_entry const& entry : std::filesystem::directory_iterator(directory))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }

            std::string const fileName = entry.path().filename().string();
            if (!std::string_view(fileName).ends_with(suffix))
            {
                continue;
            }

            // The defaults describe a manifest that could not be read; the parse below overwrites them.
            RecordedSession session{ .id = fileName.substr(0, fileName.size() - suffix.size()), .tag = "-", .model = "-", .status = "unreadable" };
            try
            {
                json const manifest   = json::parse(RawReadTextFile(entry.path()));
                json const identity   = manifest.value("identity", json::object());
                session.id            = identity.value("id", session.id);
                session.tag           = identity.value("tag", std::string{ "-" });
                json const provenance = manifest.value("provenance", json::object());
                session.model         = provenance.value("model", std::string{ "-" });
                session.status        = manifest.value("complete", false) ? "complete" : "incomplete";
            }
            catch (std::exception const& e)
            {
                Log(LogLevel::warning, "Failed to parse session manifest {}: {}", entry.path().filename().string(), e.what());
            }
            sessions.push_back(std::move(session));
        }
    }
    catch (std::filesystem::filesystem_error const& error)
    {
        Log(LogLevel::warning, "could not list {}: {}", directory.string(), error.what());
    }

    if (sessions.empty())
    {
        std::print("no recorded sessions in {}\n", directory.string());
        return 0;
    }

    std::ranges::sort(sessions, {}, &RecordedSession::id);

    std::println("{:<20}  {:<24}  {:<24}  {}", "id", "tag", "model", "status");
    for (RecordedSession const& session : sessions)
    {
        std::println("{:<20}  {:<24}  {:<24}  {}", session.id, session.tag, session.model, session.status);
    }
    std::print("{} session(s) in {}\n", sessions.size(), directory.string());

    return 0;
}

// Describes what this binary is and what it is talking to, so the orchestrator can answer
// "which build/model/service am I?" from its own prompt instead of guessing. Only the root
// session gets this: subagents do one job and gain nothing from it. The API key is never included.
std::string DescribeRuntimeContext(SessionSettings const& settings)
{
    std::string const endpoint = std::format("{}://{}:{}{}", settings.endpoint.secure ? "https" : "http", settings.endpoint.host, settings.endpoint.port, settings.endpoint.path);
    std::string const model = settings.model.empty() ? "<unspecified>" : settings.model;

    return std::format("\n\nRuntime context: you are Kern (https://github.com/TheJCAB/Kern) agent harness build `{}`. Running model `{}` via `{}`.", KernBuildId, model, endpoint);
}

std::vector<std::string> DiscoverModels(Endpoint const& endpoint)
{
    std::string const modelsPath = GetModelsPath(endpoint.path);
    if (modelsPath.empty())
    {
        return {};
    }

    Endpoint modelsEndpoint = endpoint;
    modelsEndpoint.path = modelsPath;
    try
    {
        HttpResult const result = HttpGet(modelsEndpoint);
        if (!result.IsSuccess())
        {
            return {};
        }
        return ParseAvailableModels(result.body);
    }
    catch (std::exception const& e)
    {
        Log(LogLevel::debug, "models API unavailable: {}", e.what());
        return {};
    }
}

// A prompt file supplies text verbatim, so it may hold any UTF-8 characters and span
// several lines; only the byte order mark a Windows editor may add is dropped.
std::string ReadPromptFile(std::filesystem::path const& path)
{
    std::string text;
    try
    {
        text = RawReadTextFile(path);
    }
    catch (std::exception const& e)
    {
        throw std::runtime_error{std::format("could not read the prompt file '{}': {}", path.string(), e.what())};
    }

    if (text.starts_with("\xEF\xBB\xBF"))
    {
        text.erase(0, 3);
    }
    return text;
}

int Run(CommandLineOptions const& options)
{
    SessionSettings settings = ResolveSessionSettings(options);

    if (settings.model.empty())
    {
        std::vector<std::string> const models = DiscoverModels(settings.endpoint);
        if (models.size() == 1)
        {
            settings.model = models.front();
        }
        else if (models.size() > 1)
        {
            std::println("Multiple models available:");
            for (std::string const& model : models)
            {
                std::println("  {}", model);
            }
            return 0;
        }
    }

    if (!settings.model.empty())
    {
        std::println("Using model: {}", settings.model);
    }

    // The runtime context is appended last so it is the final thing the orchestrator reads.
    std::string const systemPrompt = RawReadTextFile(GetRuntimeDataDirectory() / "MainSystemPrompt.txt") + settings.commonSystemPrompt
                                     + BuildToolPromptFragments(MainTools) + settings.workspaceInstructions
                                     + DescribeRuntimeContext(settings);

    // Built before the session so that this function keeps a handle on the subagent registry: the session takes its
    // own copy, and the guard below releases every live subagent when this function returns, however it returns. The
    // guard is declared before the session on purpose, so it is destroyed after it.
    ToolsRuntimeContext toolContext = MakeToolsRuntimeContext(settings);

    SubagentReleaser const releaseSubagents{ toolContext.releaseSubagentSessions };

    Session session{Session::Config{
        .endpointDescriptor = settings.endpoint,
        .toolContext        = std::move(toolContext),
        .systemPrompt       = systemPrompt,
        .modelName          = settings.model,
        .tools              = MainTools,
        .maxTokens          = settings.maxTokens,
        .maxTurns           = settings.maxTurns,
        .identity           = SessionIdentity{ .role = "main", .tag = settings.sessionTag },
        .sessionDirectory   = settings.sessionDirectory,
        .record             = settings.record,
    }};

    std::string initialPrompt = options.prompt;
    std::span<std::filesystem::path const> promptFilePaths = options.promptFiles;
    if (initialPrompt.empty() && !promptFilePaths.empty())
    {
        initialPrompt = ReadPromptFile(promptFilePaths.front());
        promptFilePaths = promptFilePaths.subspan(1);
    }

    std::vector<PromptFile> promptFiles;
    promptFiles.reserve(promptFilePaths.size());
    for (std::filesystem::path const& path : promptFilePaths)
    {
        promptFiles.push_back(PromptFile{
            .filename = path.string(),
            .content = ReadPromptFile(path),
        });
    }
    std::string prompt = initialPrompt;

    // A prompt given on the command line or in a prompt file is never a command, however it starts.
    PromptCommand command = ParsePromptCommand(initialPrompt, /*allowCommands=*/false);

    for (;;)
    {
        if (command.kind == PromptCommandKind::blank)
        {
            std::print("\n\nEnter prompt: ");
            if (!std::getline(std::cin, prompt))
            {
                break;
            }
            command = ParsePromptCommand(prompt);
        }

        // Commands are handled here, so that only real prompts reach the session.
        if (command.kind != PromptCommandKind::prompt)
        {
            switch (command.kind)
            {
                case PromptCommandKind::help:
                    PrintPromptCommands();
                    break;
                case PromptCommandKind::bye:
                    return 0;
                case PromptCommandKind::moreTurns:
                {
                    // '/continue [N] [TEXT]' grants the session N more turns, defaulting to
                    // TurnLimitRecovery::DefaultGrantTurns, now that it has run out of them, and any TEXT
                    // left over after the number steers the prompt that resumes the session.
                    std::string_view const argument = command.argument;
                    std::size_t const separator = argument.find_first_of(" \t");
                    std::string_view const token = argument.substr(0, separator);

                    unsigned grantedTurns = TurnLimitRecovery::DefaultGrantTurns;
                    std::string_view steering = argument;
                    if (!token.empty())
                    {
                        unsigned parsedTurns = 0;
                        std::from_chars_result const parsed =
                            std::from_chars(token.data(), token.data() + token.size(), parsedTurns);
                        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || parsedTurns == 0)
                        {
                            std::println("Invalid turn count '{}': /continue takes a positive whole number of turns, or no argument at all.", token);
                            break;
                        }
                        grantedTurns = parsedTurns;
                        steering = std::string_view{};
                        std::size_t const firstText = argument.find_first_not_of(" \t", separator);
                        if (firstText != std::string_view::npos)
                        {
                            steering = argument.substr(firstText);
                        }
                    }

                    session.GrantTurns(grantedTurns);

                    std::string resumeText = std::format(
                        "{} more turns have been granted by the user because the previous prompt spent the whole turn budget. "
                        "Resume the work you were doing rather than starting over, and finish with a final report of what "
                        "is done and what remains.",
                        grantedTurns);
                    if (!steering.empty())
                    {
                        resumeText += std::format(" The user adds: {}", steering);
                    }

                    command = PromptCommand{ .kind = PromptCommandKind::prompt, .text = std::move(resumeText) };
                    continue;
                }
                case PromptCommandKind::unknown:
                    std::println("Unknown command: /{}{}. Type /help for the list of commands.",
                        command.name, command.argument.empty() ? "" : " " + command.argument);
                    break;
                case PromptCommandKind::prompt:
                case PromptCommandKind::blank:
                    // Unreachable: prompt text goes to the session and blank lines ask for input first.
                    break;
            }
            command = PromptCommand{};
            continue;
        }

        // The response is streamed to the console by Session::Prompt, so there is nothing to return.
        // TODO: Make console output optional or configurable.
        Session::PromptResult result;
        try
        {
            // Hold the inhibitor only while this prompt is interacting with the model. It is deliberately
            // scoped to one Session::Prompt call: while the REPL is waiting for user input, `/help`, `/bye`,
            // or `/continue`, the process may let the operating system suspend normally.
            std::optional<SleepInhibitor> keepAwake;
            if (options.keepAwake)
            {
                keepAwake.emplace();
                Log(LogLevel::debug, "keep-awake: {}", keepAwake->IsActive() ? "engaged" : "unavailable");
            }
                std::vector<PromptFile> promptFilesForCall = std::move(promptFiles);
                result = session.Prompt(command.text, promptFilesForCall);
        }
        catch (std::exception const& error)
        {
            std::println("The session failed: {}", error.what());
            // A failure outside the agent loop must not end the session either: the user gets
            // another prompt and the conversation is kept.
            command = PromptCommand{};
            continue;
        }

        if (!result.ok)
        {
            std::fprintf(stderr, "error: %s (%s after %u turns)\n", result.content.c_str(), result.errorCode.c_str(), result.turnsUsed);
            // A spent turn budget is resumable with /continue, while any other failure is not.
            if (result.errorCode == std::string(TurnLimitErrorCode))
            {
                std::println("The session ran out of turns. Type /continue [N] to grant N more turns "
                             "(default {}) and resume the session.",
                    TurnLimitRecovery::DefaultGrantTurns);
                command = PromptCommand{};
                continue;
            }
            return 1;
        }

        command = PromptCommand{};
    }

    // End of input: the session is over, which is not an error.
    std::println("\nNo more input.");
    return 0;
}

}

int main(int argc, char** argv)
{
    ConfigureConsoleForUtf8();

    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 0 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i)
    {
        arguments.emplace_back(argv[i]);
    }

    try
    {
        CommandLineOptions const options = ParseCommandLine(arguments);

        switch (options.action)
        {
            case CommandLineAction::printHelp:
            {
                PrintUsage();
                return 0;
            }
            case CommandLineAction::listServices:
            {
                return ListServices();
            }
            case CommandLineAction::showSessions:
            {
                return ShowSessions(options);
            }
            case CommandLineAction::run:
            {
                break;
            }
        }

        return Run(options);
    }
    catch (UsageError const& e)
    {
        std::print(stderr, "{}\n\n", e.what());
        PrintUsage(stderr);
        return 2;
    }
    catch (std::exception const& e)
    {
        std::print(stderr, "Fatal error: {}\n", e.what());
        return 1;
    }
}