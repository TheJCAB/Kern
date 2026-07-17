
#include "FileUtilities.h"
#include "NetworkUtilities.h"
#include "Session.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"
#include "Tools/edit_file_lines.h"
#include "Tools/glob.h"
#include "Tools/grep.h"
#include "Tools/read_file_chunk.h"
#include "Tools/read_file.h"
#include "Tools/subagent.h"
#include "Tools/write_file.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using json = nlohmann::json;

namespace
{

constexpr ToolDefinition MainTools[] =
{
    //read_file,
    glob,
    grep,
    subagent,
    read_file_chunk,
};

}

Endpoint const ollamaEndpoint  { .host = "127.0.0.1", .port = "11434", .path = "/api/chat" };
Endpoint const llamacppEndpoint{ .host = "127.0.0.1", .port =  "8080", .path = "/v1/chat/completions" };

int main(int argc, char** argv)
{
    ConfigureConsoleForUtf8();

    Endpoint endpoint = llamacppEndpoint;
    std::string model = "gemma4";
    std::string prompt;
    int max_turns = 50;

    try
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string const arg = argv[i];
            if (arg == "--endpoint" && i + 1 < argc)
            {
                std::string_view const endpointName = argv[++i];
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
                    endpoint = ParseEndpoint(endpointName);
                }
            }
            else if (arg == "--port" && i + 1 < argc)
            {
                endpoint.port = std::to_string(std::stoi(argv[++i]));
            }
            else if (arg == "--max-turns" && i + 1 < argc)
            {
                max_turns = std::stoi(argv[++i]);
            }
            else if (arg == "--model" && i + 1 < argc)
            {
                model = argv[++i];
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

    ToolsRuntimeContext toolContext{
        .createNewSession = [&](std::string_view systemPrompt, std::span<ToolDefinition const> tools)
        {
            return Session{Session::Config{
                .endpointDescriptor = endpoint,
                .toolContext        = toolContext,
                .systemPrompt       { systemPrompt },
                .modelName          = model,
                .tools              = tools,
            }};
        },
        .fs{ std::filesystem::current_path() }
    };

    Session session{Session::Config{
        .endpointDescriptor = endpoint,
        .toolContext        = toolContext,
        .systemPrompt       = RawReadTextFile(GetExecutableDirectory() / "data" / "SystemPrompt.txt"),
        .modelName          = model,
        .tools              = MainTools,
    }};

    session.Prompt(prompt, max_turns);

    return 0;
}