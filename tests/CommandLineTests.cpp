#include "CommandLine.h"
#include "Logging.h"

#include "TestUtilities.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

template <typename F>
bool Throws(F&& f)
{
    try
    {
        f();
    }
    catch (std::exception const&)
    {
        return true;
    }
    return false;
}

// True only when parsing throws UsageError, as opposed to any other exception.
template <typename F>
bool ThrowsUsageError(F&& f)
{
    try
    {
        f();
    }
    catch (UsageError const&)
    {
        return true;
    }
    catch (std::exception const&)
    {
        return false;
    }
    return false;
}

// Builds a span-compatible vector so that the tests can pass brace lists directly.
CommandLineOptions Parse(std::initializer_list<std::string_view> arguments)
{
    std::vector<std::string_view> const args(arguments.begin(), arguments.end());
    return ParseCommandLine(args);
}

} // namespace

int main()
{
    std::println("Test Help");
    {
        Expect(Parse({ "--help" }).action == CommandLineAction::printHelp, "--help");
        Expect(Parse({ "-h" }).action == CommandLineAction::printHelp, "-h");
        Expect(Parse({ "--help", "junk", "--bogus" }).action == CommandLineAction::printHelp, "--help wins over junk");
        Expect(Parse({ "--bogus", "-h" }).action == CommandLineAction::printHelp, "-h wins over junk");
    }

    std::println("Test ListServices");
    {
        Expect(Parse({ "--list-services" }).action == CommandLineAction::listServices, "--list-services");
        Expect(Parse({ "--list-services", "--model", "x" }).action == CommandLineAction::listServices, "--list-services with other options");
        Expect(Parse({ "--service", "foo", "--list-services" }).action == CommandLineAction::listServices, "--list-services after --service");
    }


    std::println("Test ShowSessions");
    {
        Expect(Parse({ "--show-sessions" }).action == CommandLineAction::showSessions, "--show-sessions");
        Expect(Parse({ "--show-sessions", "--model", "x" }).action == CommandLineAction::showSessions, "--show-sessions with other options");
    }
    std::println("Test Prompt");
    {
        ExpectEqual(Parse({ "hello" }).prompt, "hello", "single positional argument");
        ExpectEqual(Parse({ "hello", "world" }).prompt, "hello world", "positional arguments are joined with single spaces");
        ExpectEqual(Parse({ "a", "b", "c" }).prompt, "a b c", "three positional arguments");
        Expect(Parse({ "hello" }).action == CommandLineAction::run, "a prompt means the run action");
        ExpectEqual(Parse({ "hello" }).maxTurns, DefaultMaxTurns, "default max turns");
    }

    std::println("Test DoubleDash");
    {
        ExpectEqual(Parse({ "--", "--not-an-option" }).prompt, "--not-an-option", "arguments after -- are prompt text");
        ExpectEqual(Parse({ "--", "--help" }).prompt, "--help", "-- shields --help");
        ExpectEqual(Parse({ "--", "--port" }).prompt, "--port", "a known option after -- is prompt text");
        Expect(Parse({ "--", "--port" }).action == CommandLineAction::run, "-- does not change the action");
        ExpectEqual(Parse({ "--", "a", "b" }).prompt, "a b", "-- prompt arguments are joined");
        ExpectEqual(Parse({ "a", "--", "--b" }).prompt, "a --b", "prompt before -- is kept");
    }

    std::println("Test ParseServiceAndEndpoint");
    {
        auto const service = Parse({ "--service", "foo", "hi" });
        ExpectEqual(service.serviceName, "foo", "--service sets the service name");
        Expect(service.endpointUrl.empty(), "--service leaves the endpoint URL empty");

        auto const endpoint = Parse({ "--endpoint", "http://localhost:1234/v1/chat/completions", "hi" });
        ExpectEqual(endpoint.endpointUrl, "http://localhost:1234/v1/chat/completions", "--endpoint sets the endpoint URL");
        Expect(endpoint.serviceName.empty(), "--endpoint leaves the service name empty");

        auto const ollama = Parse({ "--endpoint", "ollama", "hi" });
        ExpectEqual(ollama.serviceName, "ollama", "ollama is accepted as a service name");
        Expect(ollama.endpointUrl.empty(), "ollama does not set the endpoint URL");
    }

    std::println("Test ParseOptions");
    {
        auto const port = Parse({ "--port", "8080", "hi" });
        Expect(port.port.has_value(), "port is set");
        ExpectEqual(*port.port, 8080u, "--port sets the port");

        ExpectEqual(Parse({ "--port", "65535", "hi" }).port.value_or(0), 65535u, "--port accepts the highest valid port");

        auto const turns = Parse({ "--max-turns", "7", "hi" });
        ExpectEqual(turns.maxTurns, 7u, "--max-turns sets maxTurns");

        auto const model = Parse({ "--model", "x", "hi" });
        Expect(model.model.has_value(), "model is set");
        ExpectEqual(model.model.value_or(""), "x", "--model sets the model");

        auto const level = Parse({ "--log-level", "debug", "hi" });
        Expect(level.logLevel.has_value(), "log level is set");
        Expect(level.logLevel == LogLevel::debug, "--log-level sets the log level");
    }


    std::println("Test SessionRecordingOptions");
    {
        Expect(!Parse({ "--record", "off", "hi" }).record, "--record off disables recording");
        Expect(Parse({ "--record", "on", "hi" }).record, "--record on enables recording");
        Expect(ThrowsUsageError([&] { Parse({ "--record", "auto", "hi" }); }), "--record rejects auto");
        Expect(Parse({ "hi" }).record, "recording is on by default");
        Expect(ThrowsUsageError([&] { Parse({ "--record", "maybe", "hi" }); }), "--record rejects unknown modes");
        Expect(ThrowsUsageError([&] { Parse({ "--record" }); }), "--record without a value");

        auto const tagged = Parse({ "--session-dir", "/tmp/x", "--session-tag", "tool-schema-v3", "hi" });
        ExpectEqual(tagged.sessionDirectory.string(), std::string("/tmp/x"), "--session-dir sets the session directory");
        ExpectEqual(tagged.sessionTag, std::string("tool-schema-v3"), "--session-tag sets the session tag");
        ExpectEqual(tagged.prompt, std::string("hi"), "a prompt after the session options is captured");
        ExpectEqual(Parse({ "hi" }).sessionDirectory.string(), std::string(), "the session directory is empty by default");
        ExpectEqual(Parse({ "hi" }).sessionTag, std::string(), "the session tag is empty by default");

        auto const promptFirst = Parse({ "hi", "--session-tag", "tool-schema-v3" });
        ExpectEqual(promptFirst.prompt, std::string("hi"), "the prompt before the session options is kept");
        ExpectEqual(promptFirst.sessionTag, std::string("tool-schema-v3"), "--session-tag after the prompt is parsed");
    }

    std::println("Test SessionKeepAwakeOptions");
    {
        Expect(Parse({ "hi" }).keepAwake, "keep-awake is on by default");
        Expect(!Parse({ "--keep-awake", "off", "hi" }).keepAwake, "--keep-awake off disables keep-awake");
        Expect(Parse({ "--keep-awake", "on", "hi" }).keepAwake, "--keep-awake on enables keep-awake");
        Expect(ThrowsUsageError([&] { Parse({ "--keep-awake", "maybe", "hi" }); }), "--keep-awake rejects unknown modes");
        Expect(ThrowsUsageError([&] { Parse({ "--keep-awake" }); }), "--keep-awake without a value");
    }

    std::println("Test PromptFile");
    {
        std::filesystem::path const temp = CreateTempDirectory("kern-command-line-prompt-files");
        std::filesystem::path const notes = temp / "notes.txt";
        std::filesystem::path const extra = temp / "extra.txt";
        std::ofstream{ notes } << "notes";
        std::ofstream{ extra } << "extra";

        std::string const notesText = notes.string();
        std::string const extraText = extra.string();

        auto const promptFiles = Parse({ "--prompt-file", notesText });
        Expect(promptFiles.promptFiles.size() == 1, "--prompt-file sets one prompt file");
        Expect(promptFiles.promptFiles.front() == notes, "--prompt-file keeps the path");
        Expect(promptFiles.prompt.empty(), "--prompt-file leaves the prompt empty");

        auto const withModel = Parse({ "--prompt-file", notesText, "--model", "m" });
        Expect(withModel.promptFiles.front() == notes, "--prompt-file with --model keeps the path");
        Expect(withModel.model.has_value(), "--prompt-file with --model sets the model");
        ExpectEqual(withModel.model.value_or(""), "m", "--prompt-file with --model keeps the model value");

        auto const repeated = Parse({ "--prompt-file", notesText, "--prompt-file", extraText, "hi" });
        Expect(repeated.promptFiles.size() == 2, "multiple --prompt-file options are kept");
        Expect(repeated.promptFiles.at(0) == notes, "the first repeated prompt file is kept");
        Expect(repeated.promptFiles.at(1) == extra, "the second repeated prompt file is kept");
        ExpectEqual(repeated.prompt, std::string{"hi"}, "a prompt and prompt files may be combined");

        Expect(Parse({ "hi" }).promptFiles.empty(), "the prompt file list is empty by default");

        Expect(ThrowsUsageError([&] { Parse({ "--prompt-file" }); }), "--prompt-file without a value");
        Expect(ThrowsUsageError([&] { Parse({ "--prompt-file", "missing-kern-prompt-file.txt" }); }),
            "a missing --prompt-file");
    }

    std::println("Test UsageErrors");
    {
        Expect(Throws([&] { Parse({ "--modle", "foo", "hi" }); }), "unknown option");
        Expect(Throws([&] { Parse({ "-x", "hi" }); }), "unknown short option");
        Expect(Throws([&] { Parse({ "--port" }); }), "--port without a value");
        Expect(Throws([&] { Parse({ "--port", "80abc", "hi" }); }), "--port with trailing garbage");
        Expect(Throws([&] { Parse({ "--port", "-1", "hi" }); }), "--port negative");
        Expect(Throws([&] { Parse({ "--port", "4294967296", "hi" }); }), "--port out of range");
        Expect(Throws([&] { Parse({ "--port", "1", "--port", "2", "hi" }); }), "duplicate --port");
        Expect(Throws([&] { Parse({ "--port", "70000", "hi" }); }), "--port above 65535");
        Expect(Throws([&] { Parse({ "--max-turns", "1", "--max-turns", "2", "hi" }); }), "duplicate --max-turns");
        Expect(Throws([&] { Parse({ "--max-turns", "0", "hi" }); }), "--max-turns must be positive");
        Expect(Throws([&] { Parse({ "--max-turns", "-3", "hi" }); }), "--max-turns negative");
        Expect(Throws([&] { Parse({ "--model", "a", "--model", "b", "hi" }); }), "duplicate --model");
        Expect(Throws([&] { Parse({ "--log-level", "info", "--log-level", "debug", "hi" }); }), "duplicate --log-level");
        Expect(Throws([&] { Parse({ "--service", "a", "--endpoint", "b", "hi" }); }), "--service combined with --endpoint");
        Expect(Throws([&] { Parse({ "--endpoint", "b", "--service", "a", "hi" }); }), "--service after --endpoint");
        Expect(Throws([&] { Parse({ "--service", "a", "--service", "b", "hi" }); }), "duplicate --service");
        auto const noArguments = Parse({});
        Expect(noArguments.prompt.empty(), "an absent prompt is allowed");
        Expect(noArguments.action == CommandLineAction::run, "no arguments means the run action");
        Expect(noArguments.promptFiles.empty(), "no arguments leaves the prompt file list empty");

        auto const afterDoubleDash = Parse({ "--" });
        Expect(afterDoubleDash.prompt.empty(), "an absent prompt after -- is allowed");
        Expect(afterDoubleDash.action == CommandLineAction::run, "-- alone does not change the action");
        Expect(afterDoubleDash.promptFiles.empty(), "-- alone leaves the prompt file list empty");
        Expect(Throws([&] { Parse({ "--log-level", "bogus", "hi" }); }), "invalid --log-level");
    }

    std::println("All tests passed!");
    return 0;
}