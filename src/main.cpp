#include "BuildCommand.h"
#include "CommandLine.h"
#include "Credentials.h"
#include "FileUtilities.h"
#include "JsonUtilities.h"
#include "Logging.h"
#include "Services.h"
#include "Session.h"
#include "SessionRecorder.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"

#include "Tools/implement.h"
#include "Tools/read_file_chunk.h"
#include "Tools/research.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <format>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

// These defaults are intentional: Kern is meant to be used against a local server.
constexpr char DefaultServiceName[]  = "llama.cpp";
constexpr char FallbackModelName[]   = "gemma4";

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
    unsigned    maxTokens = 0;
    // Whether each session writes a transcript; --record off disables it.
    bool record = true;
    // Where transcripts go; empty means the logs/ directory beside the executable.
    std::filesystem::path sessionDirectory{};
    // Free-form label handed to every session identity.
    std::string sessionTag{};

    // Empty unless the owners of the workspace declared how to build it.
    std::optional<BuildCommands> buildCommands;
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

    if (settings.model.empty())
    {
        settings.model = FallbackModelName;
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

    settings.commonSystemPrompt = RawReadTextFile(GetExecutableDirectory() / "data" / "CommonSystemPrompt.txt");
    settings.workspaceInstructions = LoadWorkspaceInstructions();

    // A workspace that declares no build simply has no build tool at all, while one that
    // declares an invalid build is fatal: it must never be silently ignored.
    settings.buildCommands = LoadBuildConfiguration(std::filesystem::current_path());

    return settings;
}

// Builds the context that tools use. The factory captures the settings by value,
// so no session ever refers back to this function's locals.
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

    return ToolsRuntimeContext{
        .createNewSession = [settings = std::move(settings)](
            ToolsRuntimeContext const&      context,
            std::string_view                parentSessionId,
            std::string_view                role,
            std::string_view                systemPrompt,
            std::span<ToolDefinition const> tools)
        {
            return Session{Session::Config{
                .endpointDescriptor = settings.endpoint,
                .toolContext        = context,
                .systemPrompt       = std::string(systemPrompt) + settings.commonSystemPrompt + settings.workspaceInstructions,
                .modelName          = settings.model,
                .tools              = tools,
                .maxTokens          = settings.maxTokens,
                .identity           = SessionIdentity{ .parentId{ parentSessionId }, .role{ role }, .tag = settings.sessionTag },
                .sessionDirectory   = settings.sessionDirectory,
                .record             = settings.record,
            }};
        },
        .fs            = std::move(fs),
        .buildCommands = buildCommands,
        // This list is navigation-only: it hides these directories from glob and grep results
        // because they are typically not relevant. It deliberately has no say in whether a write is
        // allowed: ".git" is protected by the guarded file system, and "build" is not protected at
        // all.
        .ignoredDirectories = { ".git", "build" },
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
    std::filesystem::path const directory = options.sessionDirectory.empty() ? GetExecutableDirectory() / "logs" : options.sessionDirectory;

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
            catch (std::exception const&)
            {
                // Keep the unreadable defaults so the file is visible instead of silently skipped.
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

    return std::format("\n\nRuntime context: you are Kern (https://github.com/TheJCAB/Kern) agent harness build `{}`. Running model `{}` via `{}`.", KernBuildId, settings.model, endpoint);
}

int Run(CommandLineOptions const& options)
{
    SessionSettings const settings = ResolveSessionSettings(options);

    // The runtime context is appended last so it is the final thing the orchestrator reads.
    std::string const systemPrompt = RawReadTextFile(GetExecutableDirectory() / "data" / "MainSystemPrompt.txt") + settings.commonSystemPrompt + settings.workspaceInstructions + DescribeRuntimeContext(settings);

    Session session{Session::Config{
        .endpointDescriptor = settings.endpoint,
        .toolContext        = MakeToolsRuntimeContext(settings),
        .systemPrompt       = systemPrompt,
        .modelName          = settings.model,
        .tools              = MainTools,
        .maxTokens          = settings.maxTokens,
        .identity           = SessionIdentity{ .role = "main", .tag = settings.sessionTag },
        .sessionDirectory   = settings.sessionDirectory,
        .record             = settings.record,
    }};

    // The response is streamed to the console by Session::Prompt, so there is nothing to return.
    // TODO: Make console output optional or configurable.
    Session::PromptResult const result = session.Prompt(options.prompt, options.maxTurns);

    if (!result.ok)
    {
        std::fprintf(stderr, "error: %s (%s after %u turns)\n", result.content.c_str(), result.errorCode.c_str(), result.turnsUsed);
        return 1;
    }

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