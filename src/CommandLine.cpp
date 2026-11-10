#include "CommandLine.h"

#include "Services.h"

#include <charconv>
#include <cstddef>
#include <format>
#include <print>
#include <system_error>

namespace
{

// Returns the value of an option, advancing 'index'. Throws if it is missing.
std::string_view NextArgument(std::span<std::string_view const> arguments, std::size_t& index, std::string_view option)
{
    ++index;
    if (index >= arguments.size())
    {
        throw UsageError{std::format("Missing argument for {}", option)};
    }
    return arguments[index];
}

// Rejects partial parses ("80abc"), negative values and out-of-range input,
// all of which std::stoul would happily accept.
template <typename Integer>
Integer ParseInteger(std::string_view text, std::string_view option)
{
    Integer value{};
    char const* const begin = text.data();
    char const* const end   = begin + text.size();
    auto const [parsedTo, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || parsedTo != end)
    {
        throw UsageError{std::format("Invalid {} argument: {}", option, text)};
    }
    return value;
}

void RejectDuplicate(bool const alreadySpecified, std::string_view const option)
{
    if (alreadySpecified)
    {
        throw UsageError{std::format("{} may only be specified once", option)};
    }
}

void AppendPromptArgument(std::string& prompt, std::string_view const argument)
{
    if (!prompt.empty())
    {
        prompt += ' ';
    }
    prompt += argument;
}

}

CommandLineOptions ParseCommandLine(std::span<std::string_view const> arguments)
{
    // Mode flags are resolved before anything else is validated, so that
    // 'kern --help --bogus' prints the help instead of complaining about '--bogus'.
    bool listServices = false;
    bool showSessions = false;
    for (auto const argument : arguments)
    {
        if (argument == "--")
        {
            // Options after '--' are prompt text, never mode flags.
            break;
        }
        if (argument == "--help" || argument == "-h")
        {
            return CommandLineOptions{ .action = CommandLineAction::printHelp };
        }
        listServices = listServices || argument == "--list-services";
        showSessions = showSessions || argument == "--show-sessions";
    }
    if (listServices)
    {
        return CommandLineOptions{ .action = CommandLineAction::listServices };
    }
    if (showSessions)
    {
        return CommandLineOptions{ .action = CommandLineAction::showSessions };
    }

    CommandLineOptions options;
    bool maxTurnsSpecified = false;

    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
        std::string_view const argument = arguments[i];
        if (argument == "--service")
        {
            if (!options.serviceName.empty() || !options.endpointUrl.empty())
            {
                throw UsageError{"--service and --endpoint may only be specified once, and not together"};
            }
            options.serviceName = std::string(NextArgument(arguments, i, "--service"));
        }
        else if (argument == "--endpoint")
        {
            if (!options.serviceName.empty() || !options.endpointUrl.empty())
            {
                throw UsageError{"--service and --endpoint may only be specified once, and not together"};
            }
            std::string_view const value = NextArgument(arguments, i, "--endpoint");
            // These two are accepted as shorthand service names.
            if (value == "ollama" || value == "llama.cpp")
            {
                options.serviceName = std::string(value);
            }
            else
            {
                options.endpointUrl = std::string(value);
            }
        }
        else if (argument == "--port")
        {
            RejectDuplicate(options.port.has_value(), "--port");
            auto const port = ParseInteger<unsigned>(NextArgument(arguments, i, "--port"), "--port");
            if (port > 65535u)
            {
                throw UsageError{std::format("Invalid --port argument: {}", arguments[i])};
            }
            options.port = static_cast<std::uint32_t>(port);
        }
        else if (argument == "--max-turns")
        {
            RejectDuplicate(maxTurnsSpecified, "--max-turns");
            auto const turns = ParseInteger<unsigned>(NextArgument(arguments, i, "--max-turns"), "--max-turns");
            if (turns == 0)
            {
                throw UsageError{"Invalid --max-turns argument: 0"};
            }
            maxTurnsSpecified = true;
            options.maxTurns = turns;
        }
        else if (argument == "--model")
        {
            RejectDuplicate(options.model.has_value(), "--model");
            options.model = std::string(NextArgument(arguments, i, "--model"));
        }
        else if (argument == "--log-level")
        {
            RejectDuplicate(options.logLevel.has_value(), "--log-level");
            std::string_view const value = NextArgument(arguments, i, "--log-level");
            options.logLevel = ParseLogLevel(value);
            if (!options.logLevel)
            {
                throw UsageError{std::format("Invalid --log-level argument: {} (use off, error, warning, info, debug or trace)", value)};
            }
        }
        else if (argument == "--record")
        {
            std::string_view const value = NextArgument(arguments, i, "--record");
            if (value == "off")
            {
                options.record = false;
            }
            else if (value == "on" || value == "auto")
            {
                // 'auto' currently behaves as 'on', matching the design's default.
                options.record = true;
            }
            else
            {
                throw UsageError{std::format("Invalid --record argument: {} (use on, off or auto)", value)};
            }
        }
        else if (argument == "--session-dir")
        {
            options.sessionDirectory = std::string(NextArgument(arguments, i, "--session-dir"));
        }
        else if (argument == "--session-tag")
        {
            options.sessionTag = std::string(NextArgument(arguments, i, "--session-tag"));
        }
        else if (argument == "--")
        {
            // Everything that follows is prompt text, even if it looks like an option.
            for (++i; i < arguments.size(); ++i)
            {
                AppendPromptArgument(options.prompt, arguments[i]);
            }
            break;
        }
        else if (argument.size() > 1 && argument.front() == '-')
        {
            throw UsageError{std::format("Unknown option: {}", argument)};
        }
        else
        {
            AppendPromptArgument(options.prompt, argument);
        }
    }

    if (options.prompt.empty())
    {
        throw UsageError{"Required prompt is missing"};
    }
    return options;
}

void PrintUsage(std::FILE* stream)
{
    std::print(stream, "Usage: kern [--service NAME | --endpoint URL] [--model NAME] [--port N] [--max-turns N] [--log-level LEVEL] [--record MODE] [--session-dir PATH] [--session-tag TEXT] [--list-services] [--show-sessions] [--] PROMPT\n");
    std::print(stream, "  --help, -h        show this help and exit\n");
    std::print(stream, "  --list-services   list known services and exit\n");
    std::print(stream, "  --show-sessions   print a table of recorded sessions with their tags, model and completion flag\n");
    std::print(stream, "  --log-level LEVEL diagnostics on stderr: off, error, warning (default), info, debug, trace\n");
    std::print(stream, "  --service NAME    use a service from the built-ins or the services file ({})\n", GetServicesConfigPath().string());
    std::print(stream, "  --endpoint URL    use an ad-hoc endpoint that needs no API key ('ollama' and 'llama.cpp' are accepted as service names)\n");
    std::print(stream, "  --model NAME      override the model name reported by the service\n");
    std::print(stream, "  --port N          override the port of the resolved endpoint\n");
    std::print(stream, "  --max-turns N     give up after N agent turns (default {})\n", DefaultMaxTurns);
    std::print(stream, "  --record MODE     session recording: on (default), off or auto (currently the same as on)\n");
    std::print(stream, "  --session-dir PATH  write transcripts to PATH instead of the default log directory\n");
    std::print(stream, "  --session-tag TEXT  label recorded runs with TEXT, e.g. tool-schema-v3\n");
    std::print(stream, "  --                treat all remaining arguments as part of PROMPT\n");
}