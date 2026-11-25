#include "Session.h"

#include "ChatTransport.h"
#include "Conversation.h"
#include "ErrorRecovery.h"
#include "JsonUtilities.h"
#include "LogFile.h"
#include "Logging.h"
#include "ModelResponse.h"
#include "StringUtilities.h"
#include "TokenLimitRecovery.h"
#include "ToolUtilities.h"
#include "TurnRecord.h"
#include "TurnLimitRecovery.h"
#include "XmlToolCallParser.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>


struct Session::Pimpl
{
    // Builds a prefix for recorder log files.
    static std::string MakeLogPrefix(SessionIdentity const& identity)
    {
        if (identity.parentId.empty())
        {
            return std::format("{}_{}", identity.id, identity.role);
        }
        else
        {
            return std::format("{}_{}_{}", identity.parentId, identity.id, identity.role);
        }
    }

    // Builds the provenance row; credential material is never recorded, so apiKey is not copied.
    static SessionProvenance MakeRecorderProvenance(Config const& config)
    {
        SessionProvenance provenance;
        provenance.modelName = config.modelName;
        provenance.maxTokens = config.maxTokens;
        provenance.endpoint  = config.endpointDescriptor;
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
        : m_endpointDescriptor{ config.endpointDescriptor                }
        , m_toolContext       { std::move(config.toolContext)            }
        , m_modelName         { config.modelName                         }
        , m_tools             { config.tools.begin(), config.tools.end() }
        , m_maxTokens         { config.maxTokens                         }
        , m_maxTurns          { config.maxTurns                         }
        , m_id                { config.identity                          }
        , m_log               { MakeRecorderDirectory(config), MakeLogPrefix(config.identity) }
        , m_recorder          { config.identity,
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
    }

    PromptResult RunPrompt(std::string_view prompt)
    {
        if (prompt.empty())
        {
            return { .ok = false, .errorCode = "empty_prompt", .content = {}, .turnsUsed = 0 };
        }

        std::println("===============================\n[{}]\nPrompt: {}", m_log.Id(), prompt);

        m_conversationHistory.push_back({ .role{ "user" }, .content{ prompt } });

        // Each policy owns one kind of interruption: a cut-off response, a failed request, a spent turn budget.
        TokenLimitRecovery tokenLimitRecovery;

        ErrorRecovery errorRecovery;

        TurnLimitRecovery turnLimitRecovery;

        bool reportTurnOffered = false;

        while (true)
        {
            // The report turns are extra turns granted beyond the budget to a model that still owes the
            // session an answer. Once one has been offered and spent, the session is out of turns.
            if (m_maxTurns != 0 && m_currentTurn >= m_maxTurns + turnLimitRecovery.PendingWrapUpTurns())
            {
                if (reportTurnOffered)
                {
                    break;
                }
                reportTurnOffered = true;
                turnLimitRecovery.OfferWrapUp(m_currentTurn, m_maxTurns);
                continue;
            }
            m_log.SetSequence(m_currentTurn);

            TurnRecord turnRecord{ m_recorder, m_currentTurn };
            std::println("===============================\nTurn {} [{}]", m_currentTurn, m_log.Id());
            try
            {
                // Transient messages for this request only, highest precedence first: the report instruction outranks a
                // failure report, which outranks a cut-off nudge.
                std::vector<ConversationMessage> transientMessages;
                transientMessages.insert(transientMessages.end(), turnLimitRecovery.Messages().begin(), turnLimitRecovery.Messages().end());
                transientMessages.insert(transientMessages.end(), errorRecovery.Messages().begin(), errorRecovery.Messages().end());
                transientMessages.insert(transientMessages.end(), tokenLimitRecovery.Messages().begin(), tokenLimitRecovery.Messages().end());
                json const payload = BuildChatPayload(m_conversationHistory, transientMessages, { .modelName = m_modelName, .tools = m_tools, .maxTokens = m_maxTokens });
                Log(LogLevel::debug, "m_currentTurn {}: sending {} messages to model '{}' (request saved as log {}_{:03}_request.json)",
                    m_currentTurn, m_conversationHistory.size(), m_modelName, m_log.Id(), m_currentTurn);

                ++m_currentTurn;

                ChatTransport::Exchange const exchange = ChatTransport::SendStreamedChatRequest(m_endpointDescriptor, payload, m_log);

                json assembledResponse{
                    { "reason"   , to_string(exchange.response.reason) },
                    { "reasoning", exchange.response.reasoning         },
                    { "content"  , exchange.response.content           },
                };

                json::array_t tool_calls;
                for (auto&& call : exchange.response.toolCalls)
                {
                    tool_calls.push_back(json{
                        { "id"       , call.id },
                        { "name"     , call.name },
                        { "arguments", call.arguments },
                    });
                }

                assembledResponse["tool_calls"] = std::move(tool_calls);

                m_log.Write("response", assembledResponse.dump(2));

                turnRecord.SetResponseDuration(exchange.duration);
                ModelResponse modelResponse = exchange.response;

                if (exchange.usage.is_object() && !exchange.usage.empty())
                {
                    turnRecord.SetUsage(exchange.usage);
                }
                turnRecord.SetFinishReason(to_string(modelResponse.reason));
                turnRecord.SetAssistantText(modelResponse.content);

                if (!modelResponse.content.empty())
                {
                    // See if we got an old-school textual tool call.
                    auto toolResponse = ParseToolCall(modelResponse.content, m_toolContext, m_id.id, m_tools);
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
                        tokenLimitRecovery.OnProductiveTurn();
                        errorRecovery.OnProductiveTurn();
                        continue;
                    }
                }

                if (modelResponse.reason == ResponseFinishReason::stop)
                {
                    auto const nativeToolCalls = modelResponse.toolCalls.size();
                    if (!modelResponse.content.empty())
                    {
                        for (auto&& toolCall : ParseToolCallsXml(modelResponse.content))
                        {
                            if (std::ranges::find(modelResponse.toolCalls, toolCall) == modelResponse.toolCalls.end())
                            {
                                modelResponse.toolCalls.push_back(std::move(toolCall));
                            }
                        }
                    }
                    if (modelResponse.content.empty() && modelResponse.toolCalls.empty())
                    {
                        for (auto&& toolCall : ParseToolCallsXml(modelResponse.reasoning))
                        {
                            if (std::ranges::find(modelResponse.toolCalls, toolCall) == modelResponse.toolCalls.end())
                            {
                                modelResponse.toolCalls.push_back(std::move(toolCall));
                            }
                        }
                    }

                    if (modelResponse.toolCalls.size() > nativeToolCalls)
                    {
                        modelResponse.reason = ResponseFinishReason::tool_calls;
                        std::println("Found {} XML tools", modelResponse.toolCalls.size() > nativeToolCalls);
                    }
                    else
                    {
                        m_conversationHistory.push_back({
                            .role      = "assistant",
                            .content   = modelResponse.content,
                        });
                        return {
                            .ok                 = true,
                            .errorCode          = {},
                            .content            = std::move(modelResponse.content),
                            .turnsUsed          = m_currentTurn,
                            .stoppedAtTurnLimit = turnLimitRecovery.WrapUpPending(),
                        };
                    }
                }

                if (modelResponse.reason == ResponseFinishReason::tool_calls)
                {
                    if (turnLimitRecovery.WrapUpPending())
                    {
                        // The report turn went to a tool call, so there will be no report. The tool is not
                        // executed: the model was told in the instruction that it would not be.
                        std::println("The model spent its final report turn on a tool call instead of writing a report");
                        turnLimitRecovery.OnWrapUpTurn();
                        break;
                    }
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
                                .ok                 = true,
                                .errorCode         = {},
                                .content            = std::move(modelResponse.content),
                                .turnsUsed          = m_currentTurn,
                                .stoppedAtTurnLimit = turnLimitRecovery.WrapUpPending(),
                            };
                        }
                    }

                    for (auto const& toolCall : modelResponse.toolCalls)
                    {
                        std::println("\ntool call [{}]> {}({})", toolCall.id, toolCall.name, OneLine(toolCall.arguments.dump(), 60));
                        turnRecord.AddToolCall(json{ { "id", toolCall.id }, { "name", toolCall.name }, { "arguments", toolCall.arguments } });
                        std::string toolResult = CallTool(toolCall.name, toolCall.arguments, m_toolContext, m_id.id, m_tools);
                        turnRecord.AddToolResult(toolCall.name, toolResult);
                        std::println("\ntool result [{}]> {} -> {}", toolCall.id, toolCall.name, OneLine(toolResult, 60));
                        m_conversationHistory.push_back(MakeToolResultMessage(toolCall.id, toolResult));
                    }
                    // A turn that produced work resets the cut-off escalation to the polite first retry.
                    tokenLimitRecovery.OnProductiveTurn();
                    // ...and with it the escalation over failed requests.
                    errorRecovery.OnProductiveTurn();
                }
                else if (modelResponse.reason == ResponseFinishReason::length && !tokenLimitRecovery.Exhausted())
                {
                    // Token cap hit mid-thought: for one request only, show the model its partial output and tell it to act.
                    // The replacement response is what ends up in the history.
                    tokenLimitRecovery.OnCutOff(modelResponse.reasoning, modelResponse.content, m_maxTokens);
                }
                else if (modelResponse.reason == ResponseFinishReason::length)
                {
                    std::println("Response hit the token limit again without producing a tool call or an answer; giving up");
                    return {
                        .ok        = false,
                        .errorCode = "length",
                        .content   = "The model hit the token limit repeatedly without producing a tool call or an answer",
                        .turnsUsed = m_currentTurn,
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
                // A failed request is not fatal: report it to the model so that it can try again, and give up only
                // once the consecutive failures have spent every retry.
                std::println("agent error: {}", error.what());
                turnRecord.SetFinishReason("error");
                errorRecovery.OnFailure(error.what());
                if (errorRecovery.Exhausted())
                {
                    return { .ok        = false,
                             .errorCode = "request_failed",
                             .content   = std::format("The model request failed {} times in a row: {}", ErrorRecovery::MaxRetries, error.what()),
                             .turnsUsed = m_currentTurn };
                }
            }
        }

        return { .ok        = false,
                 .errorCode = std::string(TurnLimitErrorCode),
                 .content   = std::format("The model spent all {} turns this session is allowed without producing a final answer; everything done so far is in the conversation and can be continued if more turns are granted", m_maxTurns),
                 .turnsUsed = m_currentTurn };
    }

    // Writes the session manifest before the first request and rewrites it with the outcome afterwards; a throw
    // leaves the manifest marked incomplete, which the harness refuses.
    PromptResult Prompt(std::string_view prompt)
    {
        m_recorder.BeginSession(m_maxTurns, prompt);

        PromptResult result{};
        try
        {
            result = RunPrompt(prompt);
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

    // Grants extra turns on top of the configured budget: the way a REPL user resumes a session that ran out of
    // turns. A session configured with no turn limit has nothing to grant, and 0 extra turns grants nothing.
    void GrantTurns(unsigned extraTurns)
    {
        if (m_maxTurns == 0 || extraTurns == 0)
        {
            return;
        }

        m_maxTurns += extraTurns;
        std::println("[{}] granted {} more turns; budget is now {} turns and {} have been spent", m_log.Id(), extraTurns, m_maxTurns, m_currentTurn);
    }

    Endpoint                         m_endpointDescriptor;
    ToolsRuntimeContext              m_toolContext;
    std::string                      m_modelName;
    std::vector<ToolDefinition>      m_tools;
    unsigned                         m_maxTokens = 0;
    unsigned                         m_maxTurns = 0;
    unsigned                         m_currentTurn = 0;

    std::vector<ConversationMessage> m_conversationHistory;
    SessionIdentity                  m_id;
    LogFile                          m_log;
    // Writes <id>_session.json beside the per-turn log files; best effort, never throws.
    SessionRecorder                  m_recorder;
};

Session::PromptResult::operator nlohmann::json() const
{
    return {
        { "ok"                , ok                 },
        { "errorCode"         , errorCode          },
        { "content"           , content            },
        { "turnsUsed"         , turnsUsed          },
        { "stoppedAtTurnLimit", stoppedAtTurnLimit }
    };
}

Session::Session(Config config) : m_pimpl{ std::make_unique<Pimpl>(std::move(config)) } {}

Session::~Session() = default;

Session::Session(Session&&) = default;

Session& Session::operator=(Session&&) = default;

Session::PromptResult Session::Prompt(std::string_view prompt)
{
    return m_pimpl->Prompt(prompt);
}

void Session::GrantTurns(unsigned extraTurns) { m_pimpl->GrantTurns(extraTurns); }