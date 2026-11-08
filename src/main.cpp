
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
#include "Tools/research.h"
#include "Tools/implement.h"
#include "Tools/write_file.h"

#include <nlohmann/json.hpp>
#include <openvino/openvino.hpp>

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
    Tools::research::Definition,
    Tools::implement::Definition,
    read_file_chunk,
};

}

Endpoint const ollamaEndpoint  { .host = "127.0.0.1", .port = 11434, .path = "/api/chat" };
Endpoint const llamacppEndpoint{ .host = "127.0.0.1", .port =  8080, .path = "/v1/chat/completions" };

bool openVino = true;

int main(int argc, char** argv)
{
    ConfigureConsoleForUtf8();

if (openVino)
{
    ov::Core core;

    // Load your model (OpenVINO IR or ONNX)
    std::shared_ptr<ov::Model> model = core.read_model(R"(C:\Users\thejc\repos\Models\Gemma4-12B-it-int4\openvino_language_model.xml)");

    // Compile for Intel AI Boost NPU
    ov::CompiledModel compiled_model = core.compile_model(model, "NPU");

    // Create inference request
    ov::InferRequest infer_request = compiled_model.create_infer_request();

    // Set input tensor
    ov::Tensor input_tensor = infer_request.get_input_tensor();

    // Fill input_tensor with your data...
    std::string_view input_data = "Hello, there, who are you?";
    memcpy(input_tensor.data(), input_data.data(), std::min(input_tensor.get_size(), input_data.size()));

    // Run inference
    infer_request.infer();

    // Get output
    ov::Tensor output_tensor = infer_request.get_output_tensor();

    // Use output_tensor...

}

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
                endpoint.port = static_cast<std::uint32_t>(std::stoul(argv[++i]));
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

    std::string workspaceInstructions;
    if (std::filesystem::is_regular_file("Kern.txt"))
    {
        workspaceInstructions += "\n";
        workspaceInstructions += RawReadTextFile("Kern.txt");
    }
    if (std::filesystem::is_regular_file("Kern.md"))
    {
        workspaceInstructions += "\n";
        workspaceInstructions += RawReadTextFile("Kern.md");
    }

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

    session.Prompt(prompt, max_turns);

    return 0;
}