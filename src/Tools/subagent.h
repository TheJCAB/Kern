#pragma once

#include <FileUtilities.h>
#include <RuntimePaths.h>
#include <SubagentSessions.h>
#include <ToolUtilities.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Tools::subagent
{

// Every subagent failure reports the same keys as a run, so the orchestrator never has to special-case an error.
inline json ErrorResult(std::string_view errorCode, std::string_view summary)
{
    return json{
        { "ok"                , false     },
        { "status"            , "error"   },
        { "errorCode"         , errorCode },
        { "stoppedAtTurnLimit", false     },
        { "turns_used"        , 0u        },
        { "turns_allowed"     , 0u        },
        { "summary"           , summary   },
    };
}

// Reads an optional string argument. A missing or null value means "not given", while an empty one stays present,
// so an empty `sessionId` is a clear "unknown session" error rather than a silent request to start a new subagent.
inline std::optional<std::string> OptionalStringArgument(json const& arguments, std::string_view name)
{
    std::string const key{ name };

    if (!arguments.contains(key) || arguments.at(key).is_null())
    {
        return std::nullopt;
    }

    json const& value = arguments.at(key);
    if (!value.is_string())
    {
        throw ToolArgumentError{ std::format("`{}` must be a string", name) };
    }

    return value.get<std::string>();
}

// The boolean counterpart of OptionalStringArgument, with the same "absent or null means not given" shape.
inline std::optional<bool> OptionalBoolArgument(json const& arguments, std::string_view name)
{
    std::string const key{ name };

    if (!arguments.contains(key) || arguments.at(key).is_null())
    {
        return std::nullopt;
    }

    json const& value = arguments.at(key);
    if (!value.is_boolean())
    {
        throw ToolArgumentError{ std::format("`{}` must be a boolean", name) };
    }

    return value.get<bool>();
}

// The shared body of `research` and `implement`, which differ only in role, system-prompt file and tool list.
inline json RunSubagent(ToolsRuntimeContext const& context, std::string_view parentSessionId, std::string_view role, std::string_view systemPromptFileName, std::span<ToolDefinition const> tools, json const& arguments)
{
    try
    {
        // `release` and `sessionId` come first because releasing a finished subagent is the only way to stay
        // inside the live-session cap, and it needs no prompt at all.
        bool const                       release   = OptionalBoolArgument(arguments, "release").value_or(false);
        std::optional<std::string> const sessionId = OptionalStringArgument(arguments, "sessionId");

        if (release)
        {
            if (!sessionId)
            {
                return ErrorResult("invalid_argument",
                    "`release` needs the `sessionId` of the subagent session to release");
            }
            if (!context.releaseSubagentSessions)
            {
                return ErrorResult("invalid_argument", "this session cannot release subagent sessions");
            }

            std::size_t const released = context.releaseSubagentSessions(*sessionId);
            if (released == 0)
            {
                return ErrorResult(UnknownSubagentSessionErrorCode,
                    std::format("no live subagent session is named '{}': it never started or it was already released",
                                *sessionId));
            }

            return json{
                { "ok"                , true                     },
                { "sessionId"         , *sessionId               },
                { "status"            , "released"               },
                { "errorCode"         , ""                       },
                { "stoppedAtTurnLimit", false                    },
                { "turns_used"        , 0u                       },
                { "turns_allowed"     , 0u                       },
                { "summary"           , std::format("released subagent session '{}'; its history is gone and it can never be resumed", *sessionId) },
            };
        }

        std::string prompt = arguments.value("prompt", "");
        if (prompt.empty())
        {
            return ErrorResult("empty_prompt", "prompt must not be empty");
        }

        // The budget is per call and additive on a resume, so a resume that names no count simply gets the
        // default allowance on top of what the session already has.
        unsigned turns = SubagentMaxTurns;
        if (arguments.contains("turns") && !arguments.at("turns").is_null())
        {
            json const& value = arguments.at("turns");
            if (!value.is_number_integer() && !value.is_number_unsigned())
            {
                return ErrorResult("invalid_argument", "`turns` must be an integer");
            }
            std::int64_t const requested = value.get<std::int64_t>();
            if (requested < 1 || requested > static_cast<std::int64_t>(SubagentLifetimeMaxTurns))
            {
                return ErrorResult("invalid_argument",
                    std::format("`turns` must be between 1 and {}, the most one subagent session may spend over its whole lifetime",
                                SubagentLifetimeMaxTurns));
            }
            turns = static_cast<unsigned>(requested);
        }

        std::string systemPrompt = RawReadTextFile(GetRuntimeDataDirectory() / systemPromptFileName);
        systemPrompt += std::format(
            "\nYou have {} turns for this call. If they run out before the task is finished, this session is kept "
            "and can be resumed with more turns instead of being restarted, so everything you already read and "
            "wrote stays available.",
            turns);

        std::expected<SubagentRunResult, SubagentError> const run = context.createNewSession(
            context,
            parentSessionId,
            role,
            systemPrompt,
            tools,
            turns,
            sessionId ? std::optional<std::string_view>{ *sessionId } : std::nullopt,
            prompt);

        if (!run)
        {
            return ErrorResult(run.error().errorCode, run.error().message);
        }

        SubagentRunResult const& result = *run;

        // A wrap-up report, granted once the subagent spent its turn budget, is still an answer, but it is not a
        // finished task, so it must not read as "complete".
        std::string status = "incomplete";
        if (result.ok)
        {
            status = result.stoppedAtTurnLimit ? "stopped_at_turn_limit" : "complete";
        }

        return json{
            { "ok"                , result.ok                 },
            { "sessionId"         , result.sessionId          },
            { "status"            , std::move(status)         },
            { "errorCode"         , result.errorCode          },
            { "stoppedAtTurnLimit", result.stoppedAtTurnLimit },
            { "turns_used"        , result.turnsUsed          },
            { "turns_allowed"     , result.turnsAllowed       },
            { "summary"           , result.content            },
        };
    }
    catch (ToolArgumentError const& e)
    {
        return ErrorResult("invalid_argument", e.what());
    }
    catch (std::exception const& e)
    {
        return ErrorResult("exception", e.what());
    }
}

} // namespace Tools::subagent
