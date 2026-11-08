#include "SessionRecorder.h"

#include "FileUtilities.h"
#include "TestUtilities.h"

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
    inputs.systemPrompt    = "You are a helpful test double.\nSecond line.";
    inputs.toolDefinitions = json::array({ json{ { "name", "read_file" } } });
    inputs.userPrompt      = "unused";
    return inputs;
}

std::vector<std::string> SplitLines(std::string const& text)
{
    std::vector<std::string> lines;
    std::string              current;
    for (char const c : text)
    {
        if (c == '\n')
        {
            lines.push_back(current);
            current.clear();
        }
        else
        {
            current.push_back(c);
        }
    }
    if (!current.empty())
    {
        lines.push_back(current);
    }
    return lines;
}

json ReadManifest(std::filesystem::path const& path)
{
    return json::parse(RawReadTextFile(path));
}

}  // namespace

int main()
{
    auto const directory = CreateTempDirectory("kern_session_recorder_");
    auto const manifest  = directory / "abc123_session.json";
    auto const turns     = directory / "abc123_turns.jsonl";

    std::println("Test BeginSession writes the manifest");
    {
        SessionRecorder recorder{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        Expect(recorder.ManifestPath() == manifest, "manifest path is derived from the id");
        Expect(!std::filesystem::exists(manifest), "the constructor does no I/O");

        recorder.BeginSession(7, "do the thing");
        Expect(recorder.Enabled(), "a successful write keeps the recorder enabled");
        Expect(std::filesystem::exists(manifest), "BeginSession writes the manifest");

        auto const parsed = ReadManifest(manifest);
        ExpectEqual(parsed.at("schemaVersion").get<int>(), 1, "schema version");
        Expect(parsed.at("complete").get<bool>() == false, "the manifest starts incomplete");
        ExpectEqual(parsed.at("identity").at("id").get<std::string>(), std::string("abc123"), "identity id");
        ExpectEqual(parsed.at("provenance").at("model").get<std::string>(), std::string("test-model"), "model name");
        ExpectEqual(parsed.at("provenance").at("maxTurns").get<unsigned>(), 7u, "max turns");
        ExpectEqual(parsed.at("inputs").at("userPrompt").get<std::string>(), std::string("do the thing"), "user prompt");
        ExpectEqual(parsed.at("inputs").at("systemPrompt").get<std::string>(), MakeInputs().systemPrompt, "system prompt");
        Expect(parsed.contains("outcome"), "outcome is always present");
        Expect(parsed.at("outcome").is_object() && parsed.at("outcome").empty(), "the initial outcome is empty");
        Expect(!parsed.contains("hashes"), "no hashes are written");
        ExpectEqual(parsed.at("identity").at("sessionFile").get<std::string>(), std::string("abc123_session.json"), "session file");
        ExpectEqual(parsed.at("identity").at("turnsFile").get<std::string>(), std::string("abc123_turns.jsonl"), "turns file");
        Expect(!parsed.at("identity").contains("tag"), "an empty tag is omitted");
    }

    std::println("Test BeginSession twice");
    {
        SessionRecorder recorder{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        recorder.BeginSession(7, "do the thing");
        recorder.BeginSession(9, "a different prompt");
        Expect(recorder.Enabled(), "a repeated BeginSession is harmless");

        auto const parsed = ReadManifest(manifest);
        Expect(parsed.at("complete").get<bool>() == false, "the manifest is still incomplete");
        ExpectEqual(parsed.at("provenance").at("maxTurns").get<unsigned>(), 7u, "the first begin wins");
        ExpectEqual(parsed.at("inputs").at("userPrompt").get<std::string>(), std::string("do the thing"), "the first prompt wins");
    }

    std::println("Test RecordTurn appends one line per turn");
    {
        SessionRecorder recorder{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        recorder.BeginSession(7, "do the thing");
        recorder.RecordTurn(json{ { "index", 1 }, { "usage", { { "prompt_tokens", 2 }, { "completion_tokens", 3 } } } });
        recorder.RecordTurn(json{ { "index", 2 } });
        recorder.RecordTurn(json{ { "index", 3 } });

        Expect(std::filesystem::exists(turns), "RecordTurn creates the turns file");
        auto const lines = SplitLines(RawReadTextFile(turns));
        ExpectEqual(lines.size(), std::size_t{ 3 }, "three lines were appended");

        int expectedIndex = 1;
        for (auto const& line : lines)
        {
            Expect(!line.empty(), "no empty line is written");
            auto const parsed = json::parse(line);
            ExpectEqual(parsed.at("index").get<int>(), expectedIndex, "turns keep their order");
            ++expectedIndex;
        }

        auto const first = json::parse(lines.front());
        ExpectEqual(first.at("promptTokens").get<unsigned>(), 2u, "usage is normalised on the way out");
        ExpectEqual(first.at("totalTokens").get<unsigned>(), 5u, "the total is summed when absent");
    }

    std::println("Test NormaliseTokenUsage");
    {
        json canonical = json::parse(R"({"usage":{"prompt_tokens":11,"completion_tokens":5,"total_tokens":16}})");
        SessionRecorder::NormaliseTokenUsage(canonical);
        ExpectEqual(canonical.at("promptTokens").get<unsigned>(), 11u, "prompt tokens");
        ExpectEqual(canonical.at("completionTokens").get<unsigned>(), 5u, "completion tokens");
        ExpectEqual(canonical.at("totalTokens").get<unsigned>(), 16u, "total tokens");

        json alternative = json::parse(R"({"usage":{"prompt_eval_count":3,"eval_count":4}})");
        SessionRecorder::NormaliseTokenUsage(alternative);
        ExpectEqual(alternative.at("promptTokens").get<unsigned>(), 3u, "prompt_eval_count is a prompt count");
        ExpectEqual(alternative.at("completionTokens").get<unsigned>(), 4u, "eval_count is a completion count");
        ExpectEqual(alternative.at("totalTokens").get<unsigned>(), 7u, "the total is the sum when both are known");

        json withoutUsage = json::parse(R"({"index":9})");
        auto const before = withoutUsage;
        SessionRecorder::NormaliseTokenUsage(withoutUsage);
        Expect(withoutUsage == before, "a turn without usage is untouched");
        Expect(!withoutUsage.contains("promptTokens"), "no token key is invented");

        json wrongType = json::parse(R"({"usage":"not an object"})");
        SessionRecorder::NormaliseTokenUsage(wrongType);
        Expect(!wrongType.contains("promptTokens"), "a non-object usage yields no prompt count");
        Expect(!wrongType.contains("completionTokens"), "a non-object usage yields no completion count");
        Expect(!wrongType.contains("totalTokens"), "a non-object usage yields no total");

        Expect(!SessionRecorder::ReadTokenCount(json{ { "prompt_tokens", "eleven" } }, "prompt_tokens").has_value(),
               "a string count is rejected");
        Expect(!SessionRecorder::ReadTokenCount(json{ { "prompt_tokens", -1 } }, "prompt_tokens").has_value(),
               "a negative count is rejected");
    }

    std::println("Test EndSession completes the manifest");
    {
        SessionRecorder recorder{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        recorder.BeginSession(7, "do the thing");
        recorder.EndSession(json{ { "ok", true }, { "turnsUsed", 2 } });
        Expect(recorder.Enabled(), "a successful rewrite keeps the recorder enabled");

        auto const parsed = ReadManifest(manifest);
        Expect(parsed.at("complete").get<bool>() == true, "the manifest is complete");
        Expect(parsed.at("outcome").at("ok").get<bool>() == true, "the outcome is recorded");
        ExpectEqual(parsed.at("outcome").at("turnsUsed").get<int>(), 2, "the whole outcome object is preserved");
        ExpectEqual(parsed.at("schemaVersion").get<int>(), 1, "the schema version is preserved");
        ExpectEqual(parsed.at("identity").at("id").get<std::string>(), std::string("abc123"), "the identity is preserved");
        ExpectEqual(parsed.at("provenance").at("model").get<std::string>(), std::string("test-model"), "the model is preserved");
        ExpectEqual(parsed.at("provenance").at("maxTurns").get<unsigned>(), 7u, "the turn budget is preserved");
        ExpectEqual(parsed.at("inputs").at("systemPrompt").get<std::string>(), MakeInputs().systemPrompt, "the system prompt is preserved");
        ExpectEqual(parsed.at("inputs").at("userPrompt").get<std::string>(), std::string("do the thing"), "the user prompt is preserved");
        Expect(!std::filesystem::exists(manifest.string() + ".tmp"), "no temporary file is left behind");
    }

    std::println("Test a failed write disables the recorder");
    {
        auto const blocker = directory / "blocker";
        RawWriteTextFile(blocker, "not a directory");

        SessionRecorder recorder{ MakeIdentity(), MakeProvenance(), MakeInputs(), blocker / "nested" };
        Expect(recorder.Enabled(), "the recorder starts enabled");
        recorder.BeginSession(3, "do the thing");
        Expect(!recorder.Enabled(), "a failed manifest write disables the recorder");
        Expect(!std::filesystem::exists(recorder.ManifestPath()), "the manifest was not written");

        recorder.RecordTurn(json{ { "index", 1 } });
        recorder.EndSession(json{ { "ok", false } });
        Expect(!recorder.Enabled(), "the recorder stays disabled");
    }
    std::println("Test a successful write keeps the recorder enabled");
    {
        SessionRecorder recorder{ MakeIdentity(), MakeProvenance(), MakeInputs(), directory };
        recorder.BeginSession(7, "do the thing");
        Expect(recorder.Enabled(), "the recorder stays enabled after a successful manifest write");
        recorder.RecordTurn(json{ { "index", 1 } });
        Expect(recorder.Enabled(), "the recorder stays enabled after a successful turn append");
        recorder.EndSession(json{ { "ok", true } });
        Expect(recorder.Enabled(), "the recorder stays enabled after a successful final manifest write");
    }

    std::println("All tests passed!");
    return 0;
}
