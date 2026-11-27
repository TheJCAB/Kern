#pragma once

#include "Endpoint.h"
#include "Conversation.h"
#include "SessionRecorder.h"
#include "ToolUtilities.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

class Session
{
public:
    struct PromptResult
    {
        bool        ok = false;
        std::string errorCode;
        std::string content;
        unsigned    turnsUsed = 0;
        // True when the answer came from the extra report turn granted after the turn budget was spent,
        // i.e. the task was not finished.
        bool        stoppedAtTurnLimit = false;

        operator nlohmann::json() const;
    };
    struct Config
    {
        Endpoint                        endpointDescriptor;
        ToolsRuntimeContext             toolContext;
        std::string                     systemPrompt;
        std::string                     modelName;
        std::span<ToolDefinition const> tools;
        unsigned                        maxTokens = 0; // 0 = don't send a limit
        unsigned                        maxTurns = 0; // 0 = no limit
        // Which session this is: an empty `id` means "derive it from the log id", an empty `role`
        // means "main", and `tag` is a free-form label such as `tool-schema-v3`.
        SessionIdentity                 identity{};
        // Where the transcript manifest and turn file are written; empty means the `logs/`
        // directory beside the executable.
        std::filesystem::path sessionDirectory{};
        // Whether to write a session transcript for this session.
        bool record = true;
    };
    Session(Config);
    ~Session();

    Session(Session const&)            = delete;
    Session& operator=(Session const&) = delete;
    Session(Session&&);
    Session& operator=(Session&&);

    PromptResult Prompt(std::string_view prompt, std::span<PromptFile const> promptFiles = {});

    // The id the session recorder assigned to this session: what a subagent is resumed and released by, and
    // what names its transcript manifest.
    [[nodiscard]] std::string const& Id() const;

    // Adds `extraTurns` on top of the configured turn budget, which is how a REPL user resumes a
    // session that ran out of turns. A session configured with no turn limit (maxTurns == 0) is
    // unaffected, and `extraTurns` of 0 does nothing.
    void GrantTurns(unsigned extraTurns);

private:
    struct Pimpl;
    std::unique_ptr<Pimpl> m_pimpl;
};