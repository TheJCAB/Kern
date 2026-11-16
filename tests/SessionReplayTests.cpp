#include "Session.h"

#include "ChatTransport.h"
#include "Endpoint.h"
#include "FileUtilities.h"
#include "FileValidation.h"
#include "JsonUtilities.h"
#include "ModelResponse.h"
#include "SessionRecorder.h"
#include "TestUtilities.h"
#include "ToolUtilities.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

// One tool, given a fixed name and a workspace-relative path so that the turn it produces is stable across
// runs. Its result depends only on its arguments, which is what lets a replayed session record the same turn
// byte for byte: the replay re-runs the tools for real, so a tool whose result depended on prior state, on the
// clock or on the network could not be replayed at all.
json NoteTool(json const& arguments, ToolsRuntimeContext const& context, std::string_view const /*parentSessionId*/)
{
    std::string const path = arguments.at("path").get<std::string>();
    std::string const text = arguments.at("text").get<std::string>();
    context.fs.WriteTextFile(context.fs.ValidateWritablePath(path), text);
    return json{ { "written", text } };
}

ToolDefinition const noteToolDefinition{ .name = "note", .description = "Write a note into a file", .callTool = NoteTool };
std::array<ToolDefinition, 1> const noteTools{ noteToolDefinition };
std::span<ToolDefinition const> const noTools{};

Endpoint MakeEndpoint(std::string_view const path)
{
    return Endpoint{ .host = "127.0.0.1", .port = 8080, .path = std::string{ path }, .secure = false, .apiKey = "" };
}

// An explicit session id, so that the artifact names and the identity row of the manifest are reproducible.
SessionIdentity MakeIdentity(std::string_view const id)
{
    return SessionIdentity{ .id = std::string{ id }, .parentId = "", .role = "main", .tag = "replay-test" };
}

ToolsRuntimeContext MakeContext(std::filesystem::path const& root)
{
    return ToolsRuntimeContext{ .createNewSession = {}, .fs = ValidatedFileSystem{ root }, .buildCommands = {}, .ignoredDirectories = {} };
}

ChatTransport::Exchange MakeExchange(ResponseFinishReason const reason, std::string_view const content, json toolCalls, json usage, double const milliseconds)
{
    ChatTransport::Exchange exchange;
    exchange.response.reason  = reason;
    exchange.response.content = content;
    for (json const& call : toolCalls)
    {
        exchange.response.toolCalls.push_back(ToolCall{
            .id        = call.at("id").get<std::string>(),
            .name      = call.at("name").get<std::string>(),
            .arguments = call.at("arguments"),
        });
    }
    exchange.usage        = std::move(usage);
    exchange.milliseconds = milliseconds;
    return exchange;
}

json ReadJsonFile(std::filesystem::path const& path)
{
    return json::parse(RawReadTextFile(path));
}

// Reads one `<id>_turns.jsonl`, which the recorder writes as one compact JSON object per line.
std::vector<json> ReadTurns(std::filesystem::path const& path)
{
    std::vector<json> turns;
    std::string const text = RawReadTextFile(path);
    for (std::size_t begin = 0; begin < text.size();)
    {
        std::size_t const newline = text.find('\n', begin);
        std::size_t const end     = newline == std::string::npos ? text.size() : newline;
        if (end > begin)
        {
            turns.push_back(json::parse(text.substr(begin, end - begin)));
        }
        if (newline == std::string::npos)
        {
            break;
        }
        begin = newline + 1;
    }
    return turns;
}

ResponseFinishReason FinishReasonFromTranscript(std::string_view const reason)
{
    if (reason == "stop")           return ResponseFinishReason::stop;
    if (reason == "length")         return ResponseFinishReason::length;
    if (reason == "tool_calls")     return ResponseFinishReason::tool_calls;
    if (reason == "content_filter") return ResponseFinishReason::content_filter;
    return ResponseFinishReason::notFinished;
}

// Turns one recorded turn back into what the transport would have answered with. This is the smallest possible
// stand-in for the harness's reader, which arrives with the evaluation harness itself; it exists here only to
// show that a recorded turn is sufficient to drive a session again with no socket involved.
ChatTransport::Exchange ExchangeFromTranscript(json const& turn)
{
    ChatTransport::Exchange exchange;
    exchange.response.reason  = FinishReasonFromTranscript(turn.value("finishReason", std::string{ "stop" }));
    exchange.response.content = turn.value("assistantText", std::string{});
    if (turn.contains("toolCalls"))
    {
        for (json const& call : turn.at("toolCalls"))
        {
            exchange.response.toolCalls.push_back(ToolCall{
                .id        = call.at("id").get<std::string>(),
                .name      = call.at("name").get<std::string>(),
                .arguments = call.at("arguments"),
            });
        }
    }
    if (turn.contains("usage"))
    {
        exchange.usage = turn.at("usage");
    }
    exchange.milliseconds = turn.value("responseMs", 0.0);
    return exchange;
}

// The scripted answer of the note-taking scenario: one turn that calls the tool, one that finishes.
std::vector<ChatTransport::Exchange> MakeNoteScenario()
{
    return {
        MakeExchange(ResponseFinishReason::tool_calls, "",
            json{ { { "id", "call-1" }, { "name", "note" }, { "arguments", json{ { "path", "notes.txt" }, { "text", "hello" } } } } },
            json{ { "prompt_tokens", 11 }, { "completion_tokens", 5 }, { "total_tokens", 16 } }, 42.0),
        MakeExchange(ResponseFinishReason::stop, "the note is written", json::array(),
            json{ { "prompt_tokens", 24 }, { "completion_tokens", 6 }, { "total_tokens", 30 } }, 7.0),
    };
}

} // namespace

int main()
{
    std::println("Test the transcript records the request the session sent");
    {
        auto const workspace = CreateTempDirectory("kern_replay_workspace_");
        auto const directory = CreateTempDirectory("kern_replay_record_");

        std::vector<json> observed;
        std::vector<ChatTransport::Exchange> const exchanges = MakeNoteScenario();
        std::size_t nextExchange = 0;
        std::optional<std::string> failure;
        try
        {
            Session session{ Session::Config{
                .endpointDescriptor = MakeEndpoint("/v1/chat/completions"),
                .toolContext        = MakeContext(workspace),
                .systemPrompt       = "You are a test double.",
                .modelName          = "test-model",
                .tools              = noteTools,
                .maxTokens          = 0,
                .identity           = MakeIdentity("record0001"),
                .sessionDirectory   = directory,
                .record             = true,
                .sendChat           = [&](json const& payload) -> ChatTransport::Exchange
                                      {
                                          observed.push_back(payload);
                                          if (nextExchange >= exchanges.size())
                                          {
                                              throw std::runtime_error("the scripted transport ran out of scripted responses");
                                          }
                                          return exchanges.at(nextExchange++);
                                      },
            } };
            session.Prompt("write the note", 4);
        }
        catch (std::exception const& error)
        {
            failure = error.what();
        }
        Expect(!failure.has_value(), "the scripted session runs to completion");

        auto const turns = ReadTurns(directory / "record0001_turns.jsonl");
        ExpectEqual(turns.size(), std::size_t{ 2 }, "the session recorded two turns");
        ExpectEqual(observed.size(), std::size_t{ 2 }, "the seam was asked for exactly two requests");
        for (std::size_t turn = 0; turn < observed.size(); ++turn)
        {
            ExpectEqual(observed.at(turn), turns.at(turn).at("request"), "the recorded request is the payload the session actually sent");
        }
        ExpectEqual(observed.at(0).at("model").get<std::string>(), "test-model", "the model name reaches the payload");
        Expect(observed.at(0).at("stream").get<bool>(), "an OpenAI-compatible endpoint asks for a streamed response");
        ExpectEqual(turns.at(0).at("finishReason").get<std::string>(), "tool_calls", "the finish reason is recorded");
        ExpectEqual(turns.at(0).at("toolCalls").at(0).at("name").get<std::string>(), "note", "the tool call is recorded");
        ExpectEqual(turns.at(0).at("toolResults").at(0).at("result"), json{ { "written", "hello" } }, "the tool result is recorded");
        ExpectEqual(turns.at(0).at("responseMs").get<double>(), 42.0, "the round-trip time reported by the transport is recorded");
        ExpectEqual(turns.at(0).at("promptTokens").get<unsigned>(), 11u, "the usage row is normalised onto the turn");
        ExpectEqual(RawReadTextFile(workspace / "notes.txt"), "hello", "the tool really ran");
    }

    std::println("Test a session replayed from its transcript records a byte-identical transcript");
    {
        auto const workspace = CreateTempDirectory("kern_replay2_workspace_");
        auto const recorded  = CreateTempDirectory("kern_replay2_recorded_");
        auto const replayed  = CreateTempDirectory("kern_replay2_replayed_");

        std::vector<ChatTransport::Exchange> const exchanges = MakeNoteScenario();
        std::size_t nextExchange = 0;
        std::optional<std::string> failure;
        try
        {
            Session recording{ Session::Config{
                .endpointDescriptor = MakeEndpoint("/v1/chat/completions"),
                .toolContext        = MakeContext(workspace),
                .systemPrompt       = "You are a test double.",
                .modelName          = "test-model",
                .tools              = noteTools,
                .maxTokens          = 0,
                .identity           = MakeIdentity("replay0001"),
                .sessionDirectory   = recorded,
                .record             = true,
                .sendChat           = [&](json const&) -> ChatTransport::Exchange
                                      {
                                          if (nextExchange >= exchanges.size())
                                          {
                                              throw std::runtime_error("the scripted transport ran out of scripted responses");
                                          }
                                          return exchanges.at(nextExchange++);
                                      },
            } };
            recording.Prompt("write the note", 4);
        }
        catch (std::exception const& error)
        {
            failure = std::string{ "the recording session threw: " } + error.what();
        }
        Expect(!failure.has_value(), "the recording session runs to completion");

        auto const recordedTurns = ReadTurns(recorded / "replay0001_turns.jsonl");
        ExpectEqual(recordedTurns.size(), std::size_t{ 2 }, "the first run wrote two turns");

        std::vector<json> requestMismatches;
        std::size_t nextTurn = 0;
        failure.reset();
        try
        {
            Session replay{ Session::Config{
                .endpointDescriptor = MakeEndpoint("/v1/chat/completions"),
                .toolContext        = MakeContext(workspace),
                .systemPrompt       = "You are a test double.",
                .modelName          = "test-model",
                .tools              = noteTools,
                .maxTokens          = 0,
                .identity           = MakeIdentity("replay0001"),
                .sessionDirectory   = replayed,
                .record             = true,
                .sendChat           = [&](json const& payload) -> ChatTransport::Exchange
                                      {
                                          json const& turn = recordedTurns.at(nextTurn++);
                                          if (turn.at("request") != payload)
                                          {
                                              requestMismatches.push_back(payload);
                                          }
                                          return ExchangeFromTranscript(turn);
                                      },
            } };
            replay.Prompt("write the note", 4);
        }
        catch (std::exception const& error)
        {
            failure = std::string{ "the replay session threw: " } + error.what();
        }
        Expect(!failure.has_value(), "the replay session runs to completion on the transcript alone");
        Expect(requestMismatches.empty(), "the replayed session asks for exactly the requests the transcript recorded");
        ExpectEqual(RawReadTextFile(recorded / "replay0001_turns.jsonl"),
                    RawReadTextFile(replayed / "replay0001_turns.jsonl"),
                    "the turn transcript replays byte-identically");
        ExpectEqual(RawReadTextFile(recorded / "replay0001_session.json"),
                    RawReadTextFile(replayed / "replay0001_session.json"),
                    "the manifest replays byte-identically");
    }

    std::println("Test a transport failure is recorded as a failure");
    {
        auto const workspace = CreateTempDirectory("kern_replay_failure_workspace_");
        auto const directory = CreateTempDirectory("kern_replay_failure_");
        std::size_t requests = 0;
        bool threw = false;
        std::string what;
        Session session{ Session::Config{
            .endpointDescriptor = MakeEndpoint("/v1/chat/completions"),
            .toolContext        = MakeContext(workspace),
            .systemPrompt       = "You are a test double.",
            .modelName          = "test-model",
            .tools              = noTools,
            .maxTokens          = 0,
            .identity           = MakeIdentity("failure0001"),
            .sessionDirectory   = directory,
            .record             = true,
            .sendChat           = [&](json const&) -> ChatTransport::Exchange
                                  {
                                      ++requests;
                                      throw std::runtime_error("the service answered HTTP 503: busy");
                                  },
        } };
        try
        {
            session.Prompt("write the note", 3);
        }
        catch (std::exception const& error)
        {
            threw = true;
            what  = error.what();
        }
        Expect(threw, "a failing transport makes the session throw rather than swallow the failure");
        ExpectEqual(what, "the service answered HTTP 503: busy", "the transport error reaches the caller unchanged");
        ExpectEqual(requests, std::size_t{ 1 }, "the failing request is attempted once through the seam");

        auto const manifest = ReadJsonFile(directory / "failure0001_session.json");
        ExpectEqual(manifest.at("complete").get<bool>(), true, "the manifest still closes after a failure");
        ExpectEqual(manifest.at("outcome").at("errorCode").get<std::string>(), "exception", "the outcome records the failure");
        ExpectEqual(manifest.at("outcome").at("content").get<std::string>(), "the service answered HTTP 503: busy", "the recorded outcome names the transport error");

        auto const turns = ReadTurns(directory / "failure0001_turns.jsonl");
        ExpectEqual(turns.size(), std::size_t{ 1 }, "the failed turn is still recorded");
        Expect(turns.at(0).contains("request"), "the failed turn records the request that was attempted");
        Expect(!turns.at(0).contains("finishReason"), "the failed turn records no finish reason");
    }

    std::println("Test the endpoint flavour still decides the recorded decoding parameters");
    {
        auto const workspace = CreateTempDirectory("kern_replay_ollama_workspace_");
        auto const directory = CreateTempDirectory("kern_replay_ollama_");
        std::vector<json> observed;
        Session session{ Session::Config{
            .endpointDescriptor = MakeEndpoint("/api/chat"),
            .toolContext        = MakeContext(workspace),
            .systemPrompt       = "You are a test double.",
            .modelName          = "test-model",
            .tools              = noTools,
            .maxTokens          = 0,
            .identity           = MakeIdentity("ollama0001"),
            .sessionDirectory   = directory,
            .record             = true,
            .sendChat           = [&](json const& payload) -> ChatTransport::Exchange
                                  {
                                      observed.push_back(payload);
                                      return MakeExchange(ResponseFinishReason::stop, "ok", json::array(), json::object(), 3.0);
                                  },
        } };
        session.Prompt("say ok", 2);

        ExpectEqual(observed.size(), std::size_t{ 1 }, "the Ollama-shaped endpoint needed one request");
        Expect(!observed.at(0).at("stream").get<bool>(), "an Ollama endpoint asks for one complete body instead of a stream");
        auto const manifest = ReadJsonFile(directory / "ollama0001_session.json");
        ExpectEqual(manifest.at("provenance").at("decoding").at("stream").get<bool>(), false, "the recorded decoding row matches the request that was sent");
    }
}
