#include "Conversation.h"
#include "ToolUtilities.h"

#include "TestUtilities.h"

#include <array>
#include <cstdint>
#include <iterator>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace
{

json NoOpArguments(json const&, ToolsRuntimeContext const&, std::string_view const)
{
    return json::object();
}

constexpr StringToolParameter PathParameter{ "path", "The path of the file to look at" };

constexpr IntegerToolParameter BoundedEndLineParameter{ { "end_line", "The last line to include" },
                                                        std::optional<std::int64_t>{ 1 },
                                                        std::optional<std::int64_t>{ 10 } };

constexpr IntegerToolParameter UnboundedStartLineParameter{ { "start_line", "The first line to include" } };

// Styled like the real `build` tool: no parameters at all, plus standing policy of its own.
constexpr ToolDefinition BuildDefinition
{
    .name           = "build",
    .description    = "Run the build that the owners of this workspace declared, to check that your edits are correct.",
    .callTool       = NoOpArguments,
    .promptFragment = "Run the build once after a coherent set of edits.",
};

constexpr ToolParameter SampleToolRequiredParameters[]
{
    ToolParameter{ PathParameter },
};

constexpr ToolParameter SampleToolOptionalParameters[]
{
    ToolParameter{ BoundedEndLineParameter },
};

// Standing policy, never mechanics: this text must never reach the payload.
constexpr std::string_view SampleToolFragment = "Prefer the smallest coherent edit: re-read only the lines you touched, and never restate a policy that the standing rules already spell out.";

constexpr ToolDefinition SampleTool
{
    .name               = "sample_tool",
    .description        = "Read some lines from a file and return them as a numbered chunk.",
    .requiredParameters = SampleToolRequiredParameters,
    .optionalParameters = SampleToolOptionalParameters,
    .callTool           = NoOpArguments,
    .promptFragment     = SampleToolFragment,
};

constexpr ToolParameter OptionalOnlyParameters[]
{
    ToolParameter{ UnboundedStartLineParameter },
};

constexpr ToolDefinition OptionalOnlyTool
{
    .name               = "optional_only",
    .description        = "A tool whose single parameter may be left out of the call.",
    .optionalParameters = OptionalOnlyParameters,
    .callTool           = NoOpArguments,
};

constexpr ToolDefinition NoParametersTool
{
    .name        = "no_parameters",
    .description = "A tool that takes no arguments at all, like the real build tool.",
    .callTool    = NoOpArguments,
};

constexpr ToolDefinition BareFirstTool
{
    .name        = "bare_first",
    .description = "A tool with no standing policy of its own.",
    .callTool    = NoOpArguments,
};

constexpr ToolDefinition BareSecondTool
{
    .name        = "bare_second",
    .description = "Another tool with no standing policy of its own.",
    .callTool    = NoOpArguments,
};

constexpr ToolDefinition PayloadTools[]{ SampleTool, NoParametersTool };

constexpr ToolDefinition OptionalOnlyTools[]{ OptionalOnlyTool };

// The tools a subagent may always use; every one of them has an empty prompt fragment.
constexpr ToolDefinition BaseTools[]{ BareFirstTool, BareSecondTool };

} // namespace

int main()
{
    std::println("the prompt and prompt files are combined into one user conversation message");
    {
        PromptFile files[]{
            { .filename = "notes.txt", .content = "remember this" },
            { .filename = "data.md", .content = "# data" },
        };
        ConversationMessage const message = MakePromptMessage("do the thing", files);

        ExpectEqual(message.role, std::string{ "user" }, "prompt files must be carried in a user message");
        Expect(message.content.starts_with("do the thing\n\nPrompt files:\n"),
            "the combined message must keep the prompt before the prompt-file JSON");

        std::string_view const jsonText = std::string_view{ message.content }.substr(
            std::string_view{ "do the thing\n\nPrompt files:\n" }.size());
        json const promptFiles = json::parse(jsonText);
        Expect(promptFiles.is_array(), "prompt files must be represented as a JSON list");
        ExpectEqual(promptFiles.size(), std::size_t{ 2 }, "every prompt file must appear once");
        ExpectEqual(promptFiles.at(0).at("filename").get<std::string>(), std::string{ "notes.txt" },
            "a prompt file must retain its filename");
        ExpectEqual(promptFiles.at(0).at("content").get<std::string>(), std::string{ "remember this" },
            "a prompt file must retain its content");
        ExpectEqual(promptFiles.at(1).at("filename").get<std::string>(), std::string{ "data.md" },
            "every filename must round-trip");
        ExpectEqual(promptFiles.at(1).at("content").get<std::string>(), std::string{ "# data" },
            "every content value must round-trip");

        ConversationMessage history[]{
            message,
        };
        json const payload = BuildChatPayload(history, {}, {});
        Expect(!payload.contains("prompt_files"),
            "prompt_files must not be emitted as a non-standard top-level request property");
        ExpectEqual(payload.at("messages").size(), std::size_t{ 1 },
            "the prompt and prompt files must remain in one messages entry");
        ExpectEqual(payload.at("messages").at(0).at("role").get<std::string>(), std::string{ "user" },
            "the prompt-file payload message must remain a user message");
        Expect(payload.at("messages").at(0).at("content").get<std::string>().find("notes.txt") != std::string::npos,
            "the chat payload must contain the prompt-file filename");
    }

    std::println("a tool definition nests name, description and parameters under the function key");
    {
        json const definitions = BuildPayloadToolDefinitions(PayloadTools);

        ExpectEqual(definitions.size(), std::size_t{ 2 }, "every tool in the list must produce one definition");

        json const& definition = definitions.at(0);
        json const& function   = definition.at("function");

        ExpectEqual(definition.at("type").get<std::string>(), std::string{ "function" }, "a definition must be typed as a function");
        Expect(function.contains("name"), "a function must carry a name");
        Expect(function.contains("description"), "a function must carry a description");
        Expect(function.contains("parameters"), "a function must carry its parameters even when it has none");
        ExpectEqual(function.at("name").get<std::string>(),
                    std::string{ SampleTool.name },
                    "the payload name must round-trip the tool name");
        ExpectEqual(function.at("description").get<std::string>(),
                    std::string{ SampleTool.description },
                    "the payload description must round-trip the tool description");
        ExpectEqual(function.at("parameters").at("type").get<std::string>(),
                    std::string{ "object" },
                    "a parameter set must be typed as an object");
    }

    std::println("a definition with no required parameter omits the required key");
    {
        json const definitions = BuildPayloadToolDefinitions(OptionalOnlyTools);

        json const& parameters = definitions.at(0).at("function").at("parameters");

        Expect(!parameters.contains("required"), "a parameter set with nothing required must not carry a required key");
        Expect(parameters.contains("properties"), "a parameter set must carry its properties");
        Expect(parameters.at("properties").contains("start_line"), "an optional parameter must appear under properties");
        ExpectEqual(parameters.at("properties").at("start_line").at("type").get<std::string>(),
                    std::string{ "number" },
                    "an integer parameter must be typed as a number");
        ExpectEqual(parameters.at("properties").at("start_line").at("description").get<std::string>(),
                    std::string{ UnboundedStartLineParameter.description },
                    "a parameter description must round-trip the parameter description");
    }

    std::println("integer bounds are emitted as JSON numbers and fragments stay out of the payload");
    {
        json const definitions = BuildPayloadToolDefinitions(PayloadTools);

        json const& bounds = definitions.at(0).at("function").at("parameters").at("properties").at("end_line");

        Expect(bounds.contains("minimum"), "a bounded integer parameter must emit its minimum");
        Expect(bounds.contains("maximum"), "a bounded integer parameter must emit its maximum");
        Expect(bounds.at("minimum").is_number_integer(), "a minimum must be emitted as a JSON integer");
        Expect(bounds.at("maximum").is_number_integer(), "a maximum must be emitted as a JSON integer");
        ExpectEqual(bounds.at("minimum").get<std::int64_t>(), std::int64_t{ 1 }, "a minimum must keep its value");
        ExpectEqual(bounds.at("maximum").get<std::int64_t>(), std::int64_t{ 10 }, "a maximum must keep its value");

        std::string const payload  = definitions.dump();
        std::string const fragment = std::string{ SampleToolFragment };

        Expect(payload.find(fragment) == std::string::npos,
               "a prompt fragment is standing policy and must never reach the tool payload");
    }

    std::println("fragments are built from the final tool list including a pushed build tool");
    {
        // The real push of `build` happens inside the research and implement tool functions, once the workspace
        // declares a build, so its shape is mirrored here instead of being reached from a test.
        std::vector<ToolDefinition> tools{ std::begin(BaseTools), std::end(BaseTools) };
        tools.push_back(BuildDefinition);

        std::string const fragments = BuildToolPromptFragments(tools);
        std::string const expected  = std::string{ "\n" } + std::string{ BuildDefinition.promptFragment } + "\n";

        ExpectEqual(fragments, expected, "a pushed build tool must contribute its fragment as one paragraph");

        json const definitions = BuildPayloadToolDefinitions(tools);
        bool       foundBuild  = false;

        for (json const& definition : definitions)
        {
            if (definition.at("function").at("name").get<std::string>() == std::string{ BuildDefinition.name })
            {
                foundBuild = true;
            }
        }

        Expect(foundBuild, "a pushed build tool must appear in the tool payload");
    }

    return 0;
}
