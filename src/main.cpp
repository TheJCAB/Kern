#include "Credentials.h"
#include "FileUtilities.h"
#include "Logging.h"
#include "NetworkUtilities.h"
#include "Services.h"
#include "Session.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"

#include "Tools/read_file_chunk.h"
#include "Tools/research.h"
#include "Tools/implement.h"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{

constexpr ToolDefinition MainTools[] =
{
    Tools::research::Definition,
    Tools::implement::Definition,
    read_file_chunk,
};

}

void PrintUsage()
{
    std::println("Usage: kern [--service NAME | --endpoint URL] [--model NAME] [--port N] [--max-turns N] [--log-level LEVEL] [--list-services] PROMPT");
    std::println("  --log-level LEVEL diagnostics on stderr: off, error, warning (default), info, debug, trace");
    std::println("  --service NAME    use a service from the built-ins or the services file ({})", GetServicesConfigPath().string());
    std::println("  --endpoint URL    use an ad-hoc endpoint that needs no API key ('ollama' and 'llama.cpp' are accepted as service names)");
}

void ListServices()
{
    auto const sources = DefaultCredentialSources();
    for (auto const& service : LoadServices())
    {
        std::string keyStatus = "no API key";
        if (!service.apiKeyEnv.empty())
        {
            // Only report whether a key is available; never its value.
            keyStatus = std::format("{} ({})", service.apiKeyEnv, FindApiKey(service, sources) ? "set" : "NOT SET");
        }
        std::println("{:<16} {}  [{}]", service.name, service.endpoint, keyStatus);
    }
}

int main(int argc, char** argv)
{
    ConfigureConsoleForUtf8();

    std::string serviceName;
    std::string endpointUrl;
    std::optional<std::uint32_t> portOverride;
    std::optional<std::string> modelOverride;
    std::string prompt;
    unsigned maxTurns = 50;

    try
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string const arg = argv[i];
            if (arg == "--list-services")
            {
                try
                {
                    ListServices();
                }
                catch(const std::exception& e)
                {
                    std::println("Fatal error: {}", e.what());
                    exit(1);
                }
                return 0;
            }
            else if (arg == "--help" || arg == "-h")
            {
                PrintUsage();
                return 0;
            }
            else if (arg == "--service")
            {
                ++i;
                if (i >= argc)
                {
                    std::println("Missing argument for --service");
                    exit(1);
                }
                serviceName = argv[i];
                endpointUrl.clear();
            }
            else if (arg == "--endpoint")
            {
                ++i;
                if (i >= argc)
                {
                    std::println("Missing argument for --endpoint");
                    exit(1);
                }
                std::string_view const endpointArg = argv[i];
                if (endpointArg == "ollama" || endpointArg == "llama.cpp")
                {
                    serviceName = endpointArg;
                    endpointUrl.clear();
                }
                else
                {
                    endpointUrl = endpointArg;
                    serviceName.clear();
                }
            }
            else if (arg == "--log-level")
            {
                ++i;
                if (i >= argc)
                {
                    std::println("Missing argument for --log-level");
                    exit(1);
                }
                auto const level = ParseLogLevel(argv[i]);
                if (!level)
                {
                    std::println("Invalid --log-level argument: {} (use off, error, warning, info, debug or trace)", argv[i]);
                    exit(1);
                }
                SetLogLevel(*level);
            }
            else if (arg == "--port")
            {
                ++i;
                if (i >= argc)
                {
                    std::println("Missing argument for --port");
                    exit(1);
                }
                auto const value = std::stoul(argv[i]);
                if (value > 65535u)
                {
                    std::println("Invalid --port argument: {}", argv[i]);
                    exit(1);
                }
                portOverride = static_cast<std::uint32_t>(value);
            }
            else if (arg == "--max-turns")
            {
                ++i;
                if (i >= argc)
                {
                    std::println("Missing argument for --max-turns");
                    exit(1);
                }
                auto const value = std::stoul(argv[i]);
                if (value == 0 || value > UINT_MAX)
                {
                    std::println("Invalid --max-turns argument: {}", argv[i]);
                    exit(1);
                }
                maxTurns = static_cast<unsigned>(value);
            }
            else if (arg == "--model")
            {
                ++i;
                if (i >= argc)
                {
                    std::println("Missing argument for --model");
                    exit(1);
                }
                modelOverride = argv[i];
            }
            else if (prompt.empty())
            {
                prompt = arg;
            }
            else
            {
                prompt += " ";
                prompt += arg;
            }
        }
    }
    catch(const std::exception& e)
    {
        std::println("Couldn't parse one or more parameters: {}", e.what());
        exit(1);
    }

    if (prompt.empty())
    {
        std::println("Required prompt is missing");
        exit(1);
    }

    Endpoint endpoint;
    std::string model = "gemma4";
    try
    {
        if (!endpointUrl.empty())
        {
            // Ad-hoc endpoints have no API key, so there's nothing to protect.
            endpoint = ParseEndpoint(endpointUrl);
        }
        else
        {
            auto const services = LoadServices();
            auto const& service = FindService(services, serviceName.empty() ? "llama.cpp" : serviceName);
            endpoint = ResolveService(service, DefaultCredentialSources());
            if (!service.model.empty())
            {
                model = service.model;
            }
        }
    }
    catch(const std::exception& e)
    {
        std::println("Couldn't set up the service: {}", e.what());
        exit(1);
    }
    if (portOverride)
    {
        endpoint.port = *portOverride;
    }
    if (modelOverride)
    {
        model = *modelOverride;
    }

    std::string workspaceInstructions;
    if (std::filesystem::is_regular_file("Kern.txt"))
    {
        // Note: this will be appended to the system prompt,
        // so we add a separator line unconditionally.
        workspaceInstructions += "\n";
        workspaceInstructions += RawReadTextFile("Kern.txt");
    }
    if (std::filesystem::is_regular_file("Kern.md"))
    {
        // Note: this will be appended to the system prompt,
        // so we add a separator line unconditionally.
        workspaceInstructions += "\n";
        workspaceInstructions += RawReadTextFile("Kern.md");
    }

    try
    {
        std::string const mainSystemPrompt = RawReadTextFile(GetExecutableDirectory() / "data" / "MainSystemPrompt.txt");

        ToolsRuntimeContext toolContext{
            .createNewSession = [&](std::string_view systemPrompt, std::span<ToolDefinition const> tools)
            {
                return Session{Session::Config{
                    .endpointDescriptor = endpoint,
                    .toolContext        = toolContext,
                    .systemPrompt       = std::string(systemPrompt) + workspaceInstructions,
                    .modelName          = model,
                    .tools              = tools,
                }};
            },
            .fs{ std::filesystem::current_path() }
        };

        Session session{Session::Config{
            .endpointDescriptor = endpoint,
            .toolContext        = toolContext,
            .systemPrompt       = mainSystemPrompt + workspaceInstructions,
            .modelName          = model,
            .tools              = MainTools,
        }};

        // We don't need the prompt response here, as it's already output within the function.
        // TODO: Make console output optional or configurable.
        (void)session.Prompt(prompt, maxTurns);

        return 0;
    }
    catch(const std::exception& e)
    {
        std::println("Fatal error: {}", e.what());
        exit(1);
    }
}