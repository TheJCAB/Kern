#include "FileUtilities.h"
#include "NetworkUtilities.h"
#include "Session.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"

#include "Tools/read_file_chunk.h"
#include "Tools/research.h"
#include "Tools/implement.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

Endpoint const ollamaEndpoint  { .host = "127.0.0.1", .port = 11434, .path = "/api/chat" };
Endpoint const llamacppEndpoint{ .host = "127.0.0.1", .port =  8080, .path = "/v1/chat/completions" };

int main(int argc, char** argv)
{
    ConfigureConsoleForUtf8();

    Endpoint endpoint = llamacppEndpoint;
    std::string model = "gemma4";
    std::string prompt;
    unsigned maxTurns = 50;

    try
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string const arg = argv[i];
            if (arg == "--endpoint")
            {
                ++i;
                if (i >= argc)
                {
                    std::println("Missing argument for --endpoint");
                    exit(1);
                }
                std::string_view const endpointName = argv[i];
                if (endpointName == "ollama")
                {
                    endpoint = ollamaEndpoint;
                }
                else if (endpointName == "llama.cpp")
                {
                    endpoint = llamacppEndpoint;
                }
                else
                {
                    try
                    {
                        endpoint = ParseEndpoint(endpointName);
                    }
                    catch(const std::exception& e)
                    {
                        std::cerr << "Invalid --endpoint argument: " << endpointName << " (" << e.what() << ")\n";
                        std::cerr << "  only 'ollama', 'llama.cpp' or a valid URL are allowed\n";
                        exit(1);
                    }
                }
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
                endpoint.port = static_cast<std::uint32_t>(value);
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
                model = argv[i];
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
        std::cerr << "Couldn't parse one or more parameters: " << e.what() << '\n';
        exit(1);
    }

    if (prompt.empty())
    {
        std::cerr << "Required prompt is missing\n";
        exit(1);
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
        std::cerr << "Fatal error: " << e.what() << '\n';
        exit(1);
    }
}
