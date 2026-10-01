#pragma once

#include "ToolUtilities.h"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

class XmlToolCallParser
{
public:
    explicit XmlToolCallParser(std::string_view input) : m_input(input) {}

    std::optional<ToolCall> Parse();

private:
    bool Consume(std::string_view expected);

    bool PeekTag(std::string_view prefix) const;

    bool ConsumeFunction(std::string& out);
    bool ConsumeParameter(std::string& out);
    bool ConsumeParameterValue(std::string& out);

    std::optional<std::string> ReadTag();

    void SkipWhitespace();

private:
    std::string_view m_input;
    std::size_t      m_pos = 0;
};

inline std::vector<ToolCall> ParseToolCallsXml(std::string_view xml)
{
    std::vector<ToolCall> result;
    XmlToolCallParser parser(xml);
    while (auto call = parser.Parse())
    {
        result.push_back(std::move(*call));
    }
    return result;
}
