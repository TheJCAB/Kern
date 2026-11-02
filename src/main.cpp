#include "CommandLine.h"
#include "Credentials.h"
#include "FileUtilities.h"
#include "Logging.h"
#include "Services.h"
#include "Session.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"

#include "Tools/implement.h"
#include "Tools/read_file_chunk.h"
#include "Tools/research.h"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
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
    std::string workspaceInstructions;
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

    settings.workspaceInstructions = LoadWorkspaceInstructions();

    return settings;
}

// Builds the context that tools use. The factory captures the settings by value,
// so no session ever refers back to this function's locals.
ToolsRuntimeContext MakeToolsRuntimeContext(SessionSettings settings)
{
    return ToolsRuntimeContext{
        .createNewSession = [settings = std::move(settings)](ToolsRuntimeContext const& context, std::string_view systemPrompt, std::span<ToolDefinition const> tools)
        {
            return Session{Session::Config{
                .endpointDescriptor = settings.endpoint,
                .toolContext        = context,
                .systemPrompt       = std::string(systemPrompt) + settings.workspaceInstructions,
                .modelName          = settings.model,
                .tools              = tools,
            }};
        },
        .fs{ std::filesystem::current_path() },
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

int Run(CommandLineOptions const& options)
{
    SessionSettings const settings = ResolveSessionSettings(options);

    Session session{Session::Config{
        .endpointDescriptor = settings.endpoint,
        .toolContext        = MakeToolsRuntimeContext(settings),
        .systemPrompt       = RawReadTextFile(GetExecutableDirectory() / "data" / "MainSystemPrompt.txt") + settings.workspaceInstructions,
        .modelName          = settings.model,
        .tools              = MainTools,
    }};

    // The response is streamed to the console by Session::Prompt, so there is nothing to return.
    // TODO: Make console output optional or configurable.
    (void)session.Prompt(options.prompt, options.maxTurns);

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

        if (options.logLevel)
        {
            SetLogLevel(*options.logLevel);
        }

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