#pragma once

#include "FileUtilities.h"
#include "NetworkUtilities.h"
#include "SessionRecorder.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class Session
{
public:
    struct PromptResult
    {
        bool        ok = false;
        std::string errorCode;
        std::string content;
        unsigned    turnsUsed = 0;

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
        // Which session this is: an empty `id` means "derive it from the log id", an empty `role`
        // means "main", and `tag` is a free-form label such as `tool-schema-v3`.
        SessionIdentity identity{};
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

    PromptResult Prompt(std::string_view prompt, unsigned maxTurns);

private:
    struct Pimpl;
    std::unique_ptr<Pimpl> m_pimpl;
};