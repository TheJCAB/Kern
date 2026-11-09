#include "TurnRecord.h"

#include <utility>

TurnRecord::TurnRecord(SessionRecorder& recorder, unsigned turn)
    : m_recorder{ recorder }
{
    m_turn = json{ { "turn", turn } };
}

TurnRecord::~TurnRecord() noexcept
{
    if (!m_recorder.Enabled())
    {
        return;
    }
    SessionRecorder::NormaliseTokenUsage(m_turn);
    m_recorder.RecordTurn(m_turn);
}

void TurnRecord::SetUsage(json const& usage)
{
    if (!usage.is_object())
    {
        return;
    }
    m_turn["usage"] = usage;
}

void TurnRecord::SetResponseMilliseconds(double milliseconds)
{
    m_turn["responseMs"] = milliseconds;
}

void TurnRecord::SetFinishReason(std::string_view reason)
{
    if (reason.empty())
    {
        return;
    }
    m_turn["finishReason"] = reason;
}

void TurnRecord::SetAssistantText(std::string_view text)
{
    if (!text.empty())
    {
        m_turn["assistantText"] = text;
    }
    else if (m_turn.contains("finishReason"))
    {
        m_turn["assistantText"] = "";
    }
}

void TurnRecord::AddToolCall(json const& call)
{
    if (!m_turn.contains("toolCalls"))
    {
        m_turn["toolCalls"] = json::array();
    }
    m_turn["toolCalls"].push_back(call);
}

void TurnRecord::AddToolResult(std::string_view name, std::string const& result)
{
    if (!m_turn.contains("toolResults"))
    {
        m_turn["toolResults"] = json::array();
    }
    json& entry = m_turn["toolResults"].emplace_back(json{ { "tool", name } });
    // The full result stays in the conversation; the turn line keeps a truncated copy.
    try
    {
        json const parsed = json::parse(result);
        if (parsed.is_object() && parsed.contains("error"))
        {
            entry["error"] = parsed.at("error").is_string() ? parsed.at("error").get<std::string>() : parsed.at("error").dump();
        }
        else
        {
            entry["result"] = parsed;
        }
    }
    catch (json::exception const&)
    {
        entry["result"] = result;
    }
    entry["resultLength"] = result.size();
    if (entry.contains("result") && entry["result"].is_string() && entry["result"].get_ref<std::string const&>().size() > 2000)
    {
        entry["result"].get_ref<std::string&>().resize(2000);
        entry["resultTruncated"] = true;
    }
}
