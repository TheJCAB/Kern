#include "TurnRecord.h"

#include "FileUtilities.h"
#include "TestUtilities.h"

#include <filesystem>
#include <format>
#include <print>
#include <string>
#include <vector>

namespace
{

SessionIdentity MakeIdentity()
{
    return SessionIdentity{ .id = "abc123", .parentId = "", .role = "main", .tag = "" };
}

SessionProvenance MakeProvenance()
{
    SessionProvenance provenance;
    provenance.modelName   = "test-model";
    provenance.maxTokens   = 4096;
    provenance.decoding    = json{ { "temperature", 0.2 } };
    provenance.endpoint    = json{ { "host", "example.invalid" } };
    return provenance;
}

SessionInputs MakeInputs()
{
    SessionInputs inputs;
    inputs.systemPrompt    = "You are a helpful test double.";
    inputs.toolDefinitions = json::array({ json{ { "name", "read_file" } } });
    inputs.userPrompt      = "unused";
    return inputs;
}

// Parses every non-empty line of the turns artifact into one object per line.
std::vector<json> ReadTurns(std::filesystem::path const& path)
{
    std::vector<json> turns;
    std::string const text = RawReadTextFile(path);
    std::string       current;
    auto const        flush = [&]
    {
        if (!current.empty())
        {
            turns.push_back(json::parse(current));
            current.clear();
        }
    };
    for (char const c : text)
    {
        if (c == '\n')
        {
            flush();
        }
        else
        {
            current.push_back(c);
        }
    }
    flush();
    return turns;
}

std::size_t CountNonEmptyLines(std::filesystem::path const& path)
{
    std::size_t       count = 0;
    std::string const text  = RawReadTextFile(path);
    std::string       current;
    auto const        flush = [&]
    {
        if (!current.empty())
        {
            ++count;
            current.clear();
        }
    };
    for (char const c : text)
    {
        if (c == '\n')
        {
            flush();
        }
        else
        {
            current.push_back(c);
        }
    }
    flush();
    return count;
}

}  // namespace

int main()
{
    auto const directory = CreateTempDirectory("kern_turn_record_");
    auto const turns     = directory / "abc123_turns.jsonl";

    std::println("Test the constructor and destructor write exactly one turn line");
    {
        SessionRecorder recorder{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        Expect(recorder.Enabled(), "a fresh recorder is enabled");
        {
            TurnRecord turnRecord{ recorder, 3u };
            Expect(!std::filesystem::exists(turns), "the constructor does no I/O");
        }
        Expect(std::filesystem::exists(turns), "the destructor writes the turns artifact");
        ExpectEqual(CountNonEmptyLines(turns), std::size_t{ 1 }, "exactly one line was written");

        auto const parsed = ReadTurns(turns);
        ExpectEqual(parsed.size(), std::size_t{ 1 }, "one object per line");
        ExpectEqual(parsed.front().at("turn").get<unsigned>(), 3u, "the turn number is recorded");
        Expect(!parsed.front().contains("assistantText"), "an untouched turn records no assistant text");
    }

    std::println("Test SetUsage normalises a usage object and ignores anything else");
    {
        SessionRecorder recorder{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        {
            TurnRecord turnRecord{ recorder, 4u };
            turnRecord.SetUsage(json{ { "prompt_tokens", 11 }, { "completion_tokens", 5 }, { "total_tokens", 16 } });
        }
        auto const parsed = ReadTurns(turns).back();
        ExpectEqual(parsed.at("promptTokens").get<unsigned>(), 11u, "prompt tokens are normalised");
        ExpectEqual(parsed.at("completionTokens").get<unsigned>(), 5u, "completion tokens are normalised");
        ExpectEqual(parsed.at("totalTokens").get<unsigned>(), 16u, "total tokens are normalised");

        SessionRecorder ignoring{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        {
            TurnRecord turnRecord{ ignoring, 5u };
            turnRecord.SetUsage(json{ "not an object" });
        }
        auto const ignored = ReadTurns(turns).back();
        ExpectEqual(ignored.at("turn").get<unsigned>(), 5u, "the later turn is the last line");
        Expect(!ignored.contains("usage"), "a non-object usage is not recorded");
        Expect(!ignored.contains("promptTokens"), "a non-object usage yields no prompt count");
    }

    std::println("Test SetFinishReason and SetAssistantText");
    {
        SessionRecorder recorder{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        {
            TurnRecord turnRecord{ recorder, 6u };
            turnRecord.SetFinishReason("");
            turnRecord.SetAssistantText("");
        }
        auto const parsed = ReadTurns(turns).back();
        Expect(!parsed.contains("finishReason"), "an empty finish reason is ignored");
        Expect(!parsed.contains("assistantText"), "without a finish reason an empty text is not recorded");

        SessionRecorder second{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        {
            TurnRecord turnRecord{ second, 7u };
            turnRecord.SetFinishReason("stop");
            turnRecord.SetAssistantText("");
        }
        auto const withReason = ReadTurns(turns).back();
        ExpectEqual(withReason.at("finishReason").get<std::string>(), std::string("stop"), "the finish reason is recorded");
        ExpectEqual(withReason.at("assistantText").get<std::string>(), std::string(""), "an empty text is recorded once a reason exists");

        SessionRecorder third{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        {
            TurnRecord turnRecord{ third, 8u };
            turnRecord.SetAssistantText("hello");
        }
        auto const withText = ReadTurns(turns).back();
        ExpectEqual(withText.at("assistantText").get<std::string>(), std::string("hello"), "non-empty text is always recorded");
        Expect(!withText.contains("finishReason"), "no finish reason was set");
    }

    std::println("Test SetResponseMilliseconds and AddToolCall");
    {
        SessionRecorder recorder{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        std::size_t const linesBefore = CountNonEmptyLines(turns);
        {
            TurnRecord turnRecord{ recorder, 9u };
            turnRecord.SetResponseMilliseconds(123.5);
            turnRecord.AddToolCall(json{ { "name", "read_file" }, { "arguments", json::object() } });
            turnRecord.AddToolCall(json{ { "name", "grep" }, { "arguments", json::object() } });
            ExpectEqual(CountNonEmptyLines(turns), linesBefore, "the setters write nothing on their own");
        }
        ExpectEqual(CountNonEmptyLines(turns), linesBefore + 1, "the destructor appends one line");
        auto const parsed = ReadTurns(turns).back();
        ExpectEqual(parsed.at("responseMs").get<double>(), 123.5, "the response time is recorded");
        Expect(parsed.at("toolCalls").is_array(), "toolCalls is an array");
        ExpectEqual(parsed.at("toolCalls").size(), std::size_t{ 2 }, "two calls were appended");
        ExpectEqual(parsed.at("toolCalls").at(0).at("name").get<std::string>(), std::string("read_file"), "the first call is kept");
        ExpectEqual(parsed.at("toolCalls").at(1).at("name").get<std::string>(), std::string("grep"), "the second call is appended in order");
    }

    std::println("Test AddToolResult lifts an error, keeps parsed objects and plain text");
    {
        SessionRecorder recorder{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        std::string const errorResult = R"({"error":"file not found"})";
        {
            TurnRecord turnRecord{ recorder, 10u };
            turnRecord.AddToolResult("read_file", errorResult);
            turnRecord.AddToolResult("grep", R"({"lines":["a","b"]})");
            turnRecord.AddToolResult("build", "plain text, not JSON");
        }
        auto const parsed     = ReadTurns(turns).back();
        auto const& results   = parsed.at("toolResults");
        ExpectEqual(results.size(), std::size_t{ 3 }, "three results were appended");

        auto const& errorEntry = results.at(0);
        ExpectEqual(errorEntry.at("tool").get<std::string>(), std::string("read_file"), "the tool name is recorded");
        ExpectEqual(errorEntry.at("error").get<std::string>(), std::string("file not found"), "the error member is lifted");
        Expect(!errorEntry.contains("result"), "an error result stores no result");
        ExpectEqual(errorEntry.at("resultLength").get<std::size_t>(), errorResult.size(), "the true length is kept");

        auto const& objectEntry = results.at(1);
        Expect(objectEntry.at("result").is_object(), "a parsed object is stored as result");
        ExpectEqual(objectEntry.at("result").at("lines").size(), std::size_t{ 2 }, "the parsed object is preserved");
        Expect(!objectEntry.contains("error"), "no error is invented");

        auto const& textEntry = results.at(2);
        ExpectEqual(textEntry.at("result").get<std::string>(), std::string("plain text, not JSON"), "a non-JSON string is stored verbatim");
        Expect(!textEntry.contains("resultTruncated"), "a short result is not truncated");
    }

    std::println("Test AddToolResult truncates a long result and keeps its true length");
    {
        std::string const longResult(2500, 'x');
        SessionRecorder recorder{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        {
            TurnRecord turnRecord{ recorder, 11u };
            turnRecord.AddToolResult("build", longResult);
        }
        json const  parsed = ReadTurns(turns).back();
        json const& entry  = parsed.at("toolResults").at(0);
        ExpectEqual(entry.at("result").get<std::string>().size(), std::size_t{ 2000 }, "the stored result is truncated to 2000 characters");
        ExpectEqual(entry.at("result").get<std::string>(), longResult.substr(0, 2000), "the kept prefix is the head of the result");
        Expect(entry.at("resultTruncated").get<bool>(), "the truncation is flagged");
        ExpectEqual(entry.at("resultLength").get<std::size_t>(), longResult.size(), "the true length is kept");
    }

    std::println("Test a disabled recorder writes no file");
    {
        auto const     disabledDirectory = CreateTempDirectory("kern_turn_record_disabled_");
        auto const     disabledTurns     = disabledDirectory / "abc123_turns.jsonl";
        SessionRecorder recorder{ MakeIdentity(), MakeProvenance(), MakeInputs(), disabledDirectory, false };
        Expect(!recorder.Enabled(), "the recorder is disabled");
        {
            TurnRecord turnRecord{ recorder, 1u };
            turnRecord.SetUsage(json{ { "prompt_tokens", 1 } });
            turnRecord.SetResponseMilliseconds(5.0);
            turnRecord.SetFinishReason("stop");
            turnRecord.SetAssistantText("hello");
            turnRecord.AddToolCall(json{ { "name", "read_file" } });
            turnRecord.AddToolResult("read_file", "text");
        }
        Expect(!std::filesystem::exists(disabledTurns), "a disabled recorder writes nothing");
    }

    std::println("All tests passed!");
    return 0;
}
