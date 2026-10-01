#include "XmlToolCallParser.h"

#include <print>

inline std::string Trim(std::string_view input)
{
    auto begin = input.begin();
    auto end = input.end();

    while (begin != end && std::isspace(static_cast<unsigned char>(*begin)))
        ++begin;

    while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1))))
        --end;

    return std::string(begin, end);
}

std::optional<ToolCall> XmlToolCallParser::Parse()
{
    m_pos = m_input.find("<tool_call>", m_pos);
    if (m_pos == m_input.npos)
    {
        return std::nullopt;
    }

    Consume("<tool_call>");

    std::print("<tool_call> found!\n");

    SkipWhitespace();

    ToolCall result;
    if (!ConsumeFunction(result.name))
    {
        std::print("Failed to consume function at {}\n", m_input.substr(m_pos, std::min<size_t>(20, m_input.size() - m_pos)));
        return std::nullopt;
    }

    SkipWhitespace();

    while (true)
    {
        SkipWhitespace();

        if (PeekTag("parameter="))
        {
            std::string name;
            if (!ConsumeParameter(name))
            {
                std::print("Failed to consume parameter at {}\n", m_input.substr(m_pos, std::min<size_t>(20, m_input.size() - m_pos)));
                return std::nullopt;
            }

            SkipWhitespace();

            std::string value;
            if (!ConsumeParameterValue(value))
            {
                std::print("Failed to consume parameter value at {}\n", m_input.substr(m_pos, std::min<size_t>(20, m_input.size() - m_pos)));
                return std::nullopt;
            }

            result.arguments[std::move(name)] = std::move(value);
            SkipWhitespace();
            continue;
        }

        if (Consume("</function>"))
            break;

        std::print("Failed to consume function end tag at {}\n", m_input.substr(m_pos, std::min<size_t>(20, m_input.size() - m_pos)));

        return std::nullopt;
    }

    SkipWhitespace();
    if (!Consume("</tool_call>"))
    {
        std::print("Failed to consume tool_call end tag at {}\n", m_input.substr(m_pos, std::min<size_t>(20, m_input.size() - m_pos)));
        return std::nullopt;
    }

    return result;
}

bool XmlToolCallParser::Consume(std::string_view expected)
{
    SkipWhitespace();
    if (m_input.substr(m_pos, expected.size()) != expected)
        return false;

    m_pos += expected.size();
    return true;
}

bool XmlToolCallParser::PeekTag(std::string_view prefix) const
{
    if (m_pos >= m_input.size() || m_input[m_pos] != '<')
        return false;

    auto end = m_input.find('>', m_pos);
    if (end == std::string_view::npos)
        return false;

    auto tag = m_input.substr(m_pos + 1, end - m_pos - 1);
    return tag.rfind(prefix, 0) == 0;
}

bool XmlToolCallParser::ConsumeFunction(std::string& out)
{
    auto tag = ReadTag();
    if (!tag)
        return false;

    auto text = Trim(*tag);
    if (text.rfind("function=", 0) != 0)
        return false;

    out = Trim(text.substr(std::string_view("function=").size()));
    return !out.empty();
}

bool XmlToolCallParser::ConsumeParameter(std::string& out)
{
    auto tag = ReadTag();
    if (!tag)
        return false;

    auto text = Trim(*tag);
    if (text.rfind("parameter=", 0) != 0)
        return false;

    out = Trim(text.substr(std::string_view("parameter=").size()));
    return !out.empty();
}

bool XmlToolCallParser::ConsumeParameterValue(std::string& out)
{
    auto end = m_input.find("</parameter>", m_pos);
    if (end == std::string_view::npos)
        return false;

    out = Trim(m_input.substr(m_pos, end - m_pos));
    m_pos = end;
    return Consume("</parameter>");
}

std::optional<std::string> XmlToolCallParser::ReadTag()
{
    SkipWhitespace();
    if (m_pos >= m_input.size() || m_input[m_pos] != '<')
        return std::nullopt;

    auto end = m_input.find('>', m_pos);
    if (end == std::string_view::npos)
        return std::nullopt;

    std::string tag = std::string(m_input.substr(m_pos + 1, end - m_pos - 1));
    m_pos = end + 1;
    return tag;
}

void XmlToolCallParser::SkipWhitespace()
{
    while (m_pos < m_input.size() && std::isspace(static_cast<unsigned char>(m_input[m_pos])))
    {
        ++m_pos;
    }
}
