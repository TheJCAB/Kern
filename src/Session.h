#pragma once

#include "FileUtilities.h"
#include "NetworkUtilities.h"
#include "StringUtilities.h"
#include "ToolUtilities.h"

#include <string>
#include <string_view>
#include <vector>

class Session
{
public:
    struct Config
    {
        Endpoint                        endpointDescriptor;
        ToolsRuntimeContext             toolContext;
        std::string                     systemPrompt;
        std::string                     modelName;
        std::span<ToolDefinition const> tools;
    };
    Session(Config);
    ~Session();

    std::string Prompt(std::string_view prompt, int maxTurns);
    
private:
    struct Pimpl;
    std::unique_ptr<Pimpl> m_pimpl;
};
