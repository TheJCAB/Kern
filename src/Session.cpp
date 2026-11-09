#include "Session.h"

#include "ChatTransport.h"
#include "Conversation.h"
#include "JsonUtilities.h"
#include "LogFile.h"
#include "Logging.h"
#include "ModelResponse.h"
#include "StringUtilities.h"
#include "TokenLimitRecovery.h"
#include "ToolUtilities.h"
#include "TurnRecord.h"
#include "XmlToolCallParser.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>


struct Session::Pimpl
{
    // Builds the recorder identity: an empty id comes from the log id, an empty role means "main".
    static SessionIdentity MakeRecorderIdentity(Config const& config, std::string_view const logId)
    {
        SessionIdentity identity = config.identity;
        if (identity.id.empty())
        {
            identity.id = std::string(logId);
        }
        if (identity.role.empty())
        {
            identity.role = "main";
        }
        return identity;
    }

    // Builds the provenance row; credential material is never recorded, so apiKey is not copied.
    static SessionProvenance MakeRecorderProvenance(Config const& config)
    {
        SessionProvenance provenance;
        provenance.modelName = config.modelName;
        provenance.maxTokens = config.maxTokens;
        provenance.endpoint  = json{
            { "host"  , config.endpointDescriptor.host   },
            { "port"  , config.endpointDescriptor.port   },
            { "path"  , config.endpointDescriptor.path   },
            { "secure", config.endpointDescriptor.secure },
        };
        return provenance;
    }

    // Builds the inputs row; the user prompt is left empty because BeginSession fills it in.
    static SessionInputs MakeRecorderInputs(Config const& config)
    {
        SessionInputs inputs;
        inputs.systemPrompt    = config.systemPrompt;
        inputs.toolDefinitions = BuildPayloadToolDefinitions(config.tools);
        return inputs;
    }

    // Picks the directory that receives the manifest: the configured one, or the logs directory.
    static std::filesystem::path MakeRecorderDirectory(Config const& config)
    {
        return config.sessionDirectory.empty() ? LogFile::Directory() : config.sessionDirectory;
    }

    Pimpl(Config config)
        : m_endpointDescriptor{ config.endpointDescriptor               }
        , m_toolContext       { std::move(config.toolContext)            }
        , m_modelName         { config.modelName                         }
        , m_tools             { config.tools.begin(), config.tools.end() }
        , m_maxTokens         { config.maxTokens                         }
        , m_log               {}
        , m_transport         { m_endpointDescriptor, m_log              }
        , m_recorder          { MakeRecorderIdentity(config, m_log.Id()),
                                MakeRecorderProvenance(config),
                                MakeRecorderInputs(config),
                                MakeRecorderDirectory(config),
                                config.record                        }
    {
        std::string systemPrompt = std::move(config.systemPrompt);
        if (m_maxTokens != 0)
        {
            // Reasoning and the reply share one output budget, so say so up front and not only after a cut-off.
            systemPrompt += "\n\nYour output budget for each response is " + std::to_string(m_maxTokens)
                          + " tokens, shared between your reasoning and your reply, so a long chain of thought can use all of it"
                            " and leave no room for the tool call or the answer you were thinking about.";
        }
        m_conversationHistory.push_back({ .role = "system", .content = std::move(systemPrompt) });

        std::println("log id: {}", m_log.Id());

        // The decoding parameters actually sent; BuildPayload hard-codes streaming and only sends max_tokens when
        // non-zero.
        m_recorder.SetDecoding(json{ { "stream", !m_transport.IsOllama() }, { "maxTokens", m_maxTokens } });
    }

    PromptResult RunPrompt(std::string_view prompt, unsigned maxTurns)
    {
        if (prompt.empty())
        {
            return { .ok = false, .errorCode = "empty_prompt", .content = {}, .turnsUsed = 0 };
        }

        std::println("===============================\n[{}]\nPrompt: {}", m_log.Id(), prompt);

        m_conversationHistory.push_back({ .role{ "user" }, .content{ prompt } });

        TokenLimitRecovery recovery;

        for (unsigned turn = 0; turn < maxTurns; ++turn, m_log.Advance())
        {
            TurnRecord turnRecord{ m_recorder, turn };
            std::println("===============================\nTurn {} [{}]", turn, m_log.Id());
            try
            {
                json const payload = BuildChatPayload(m_conversationHistory, recovery.Messages(), { .modelName = m_modelName, .tools = m_tools, .maxTokens = m_maxTokens, .stream = !m_transport.IsOllama() });
                Log(LogLevel::debug, "turn {}: sending {} messages to model '{}' (request saved as log {}_{:03}_request.json)",
                    turn, m_conversationHistory.size(), m_modelName, m_log.Id(), m_log.Sequence());

                ChatTransport::Exchange const exchange = m_transport.Send(payload);
                turnRecord.SetResponseMilliseconds(exchange.milliseconds);
                ModelResponse modelResponse = exchange.response;

                if (!modelResponse.reasoning.empty() && m_transport.IsOllama())
                {
                    std::println("\nreasoning> {}\n", modelResponse.reasoning);
                }

                if (exchange.usage.is_object() && !exchange.usage.empty())
                {
                    turnRecord.SetUsage(exchange.usage);
                }
                turnRecord.SetFinishReason(to_string(modelResponse.reason));
                turnRecord.SetAssistantText(modelResponse.content);

                if (!modelResponse.content.empty())
                {
                    // See if we got an old-school textual tool call.
                    auto toolResponse = ParseToolCall(modelResponse.content, m_toolContext, m_tools);
                    if (toolResponse)
                    {
                        auto& toolResult = toolResponse.value();

                        std::println("tool> {} -> {}\n", OneLine(modelResponse.content, 20), OneLine(toolResult, 20));

                        m_conversationHistory.push_back({
                            .role    = "user",
                            .content = toolResult,
                        });
                        m_conversationHistory.push_back({
                            .role    = "assistant",
                            .content = std::move(modelResponse.content),
                        });
                        continue;
                    }
                    else if (m_transport.IsOllama()) // Otherwise, we already did during streaming
                    {
                        std::println("\nassistant> {}\n", modelResponse.content);
                    }
                }

                if (modelResponse.reason == ResponseFinishReason::stop)
                {
                    if (modelResponse.content.empty() && modelResponse.toolCalls.empty())
                    {
                        modelResponse.toolCalls = ParseToolCallsXml(modelResponse.reasoning);
                    }
                    if (!modelResponse.toolCalls.empty())
                    {
                        modelResponse.reason = ResponseFinishReason::tool_calls;
                        std::println("Found {} XML tools", modelResponse.toolCalls.size());
                    }
                    else
                    {
                        m_conversationHistory.push_back({
                            .role      = "assistant",
                            .content   = modelResponse.content,
                        });
                        return {
                            .ok        = true,
                            .errorCode = {},
                            .content   = std::move(modelResponse.content),
                            .turnsUsed = turn + 1,
                        };
                    }
                }

                if (modelResponse.reason == ResponseFinishReason::tool_calls)
                {
                    if (modelResponse.toolCalls.empty())
                    {
                        if (modelResponse.content.empty())
                        {
                            std::println("The model said it was calling tools, but we didn't find any tool to call");
                            throw std::runtime_error("The model said it was calling tools, but we didn't find any tool to call");
                        }
                        else
                        {
                            // Figure it's really a stop...?
                            m_conversationHistory.push_back({
                                .role      = "assistant",
                                .content   = modelResponse.content,
                            });
                            return {
                                .ok        = true,
                                .errorCode = {},
                                .content   = std::move(modelResponse.content),
                                .turnsUsed = turn + 1,
                            };
                        }
                    }
                    if (!m_transport.IsOllama())
                    {
                        m_conversationHistory.push_back(MakeAssistantToolCallMessage(std::move(modelResponse.content), modelResponse.toolCalls));
                    }

                    for (auto const& toolCall : modelResponse.toolCalls)
                    {
                        std::println("\ntool call [{}]> {}({})", toolCall.id, toolCall.name, OneLine(toolCall.arguments.dump(), 60));
                        turnRecord.AddToolCall(json{ { "id", toolCall.id }, { "name", toolCall.name }, { "arguments", toolCall.arguments } });
                        std::string toolResult = CallTool(toolCall.name, toolCall.arguments, m_toolContext, m_tools);
                        turnRecord.AddToolResult(toolCall.name, toolResult);
                        std::println("\ntool result [{}]> {} -> {}", toolCall.id, toolCall.name, OneLine(toolResult, 60));
                        m_conversationHistory.push_back(MakeToolResultMessage(toolCall.id, toolResult));
                    }
                    // A turn that produced work resets the cut-off escalation to the polite first retry.
                    recovery.OnProductiveTurn();
                }
                else if (modelResponse.reason == ResponseFinishReason::length && !recovery.Exhausted())
                {
                    // Token cap hit mid-thought: for one request only, show the model its partial output and tell it to act.
                    // The replacement response is what ends up in the history.
                    recovery.OnCutOff(modelResponse.reasoning, modelResponse.content, m_maxTokens);
                }
                else if (modelResponse.reason == ResponseFinishReason::length)
                {
                    std::println("Response hit the token limit again without producing a tool call or an answer; giving up");
                    return {
                        .ok        = false,
                        .errorCode = "length",
                        .content   = "The model hit the token limit repeatedly without producing a tool call or an answer",
                        .turnsUsed = turn + 1,
                    };
                }
                else
                {
                    std::println("TODO: stop reason {}", to_string(modelResponse.reason));
                    throw std::runtime_error(std::string("TODO: stop reason ") + to_string(modelResponse.reason));
                }
            }
            catch (std::exception const& error)
            {
                std::println("agent error: {}", error.what());
                throw;
            }
        }

        return  { .ok = false, .errorCode = "too_many_turns", .content = "The model took too many turns", .turnsUsed = maxTurns };
    }

    // Writes the session manifest before the first request and rewrites it with the outcome afterwards; a throw
    // leaves the manifest marked incomplete, which the harness refuses.
    PromptResult Prompt(std::string_view prompt, unsigned maxTurns)
    {
        m_recorder.BeginSession(maxTurns, prompt);

        PromptResult result{};
        try
        {
            result = RunPrompt(prompt, maxTurns);
        }
        catch (std::exception const& error)
        {
            m_recorder.EndSession(json{
                { "ok"       , false          },
                { "errorCode", "exception"    },
                { "content"  , error.what()   },
                { "turnsUsed", 0u             },
            });
            throw;
        }

        m_recorder.EndSession(static_cast<json>(result));
        return result;
    }


    Endpoint                         m_endpointDescriptor;
    ToolsRuntimeContext              m_toolContext;
    std::string                      m_modelName;
    std::vector<ToolDefinition>      m_tools;
    unsigned                         m_maxTokens = 0;

    std::vector<ConversationMessage> m_conversationHistory;
    LogFile                          m_log;
    ChatTransport                    m_transport;
    // Writes <id>_session.json beside the per-turn log files; best effort, never throws.
    SessionRecorder                  m_recorder;
};

Session::PromptResult::operator nlohmann::json() const
{
    return {
        { "ok"       , ok        },
        { "errorCode", errorCode },
        { "content"  , content   },
        { "turnsUsed", turnsUsed }
    };
}

Session::Session(Config config) : m_pimpl{ std::make_unique<Pimpl>(std::move(config)) } {}

Session::~Session() = default;

Session::Session(Session&&) = default;

Session& Session::operator=(Session&&) = default;

Session::PromptResult Session::Prompt(std::string_view prompt, unsigned maxTurns)
{
    return m_pimpl->Prompt(prompt, maxTurns);
}