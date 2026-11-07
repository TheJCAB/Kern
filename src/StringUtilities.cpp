
#include "StringUtilities.h"

#if _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <vector>

void ConfigureConsoleForUtf8()
{
#if _WIN32
    static UINT input  = GetConsoleCP();
    static UINT output = GetConsoleOutputCP();

    SetConsoleCP      (CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    std::atexit([](){
        SetConsoleCP      (input);
        SetConsoleOutputCP(output);
    });
#endif
}

std::string EscapeString(std::string_view const& input)
{
    std::string out;
    for (char const c : input)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

std::string UnescapeString(std::string_view const& input)
{
    std::string out;
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] == '\\' && i + 1 < input.size())
        {
            ++i;
            switch (input[i])
            {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            default:
                out.push_back(input[i]);
                break;
            }
        }
        else
        {
            out.push_back(input[i]);
        }
    }
    return out;
}

std::string OneLine(std::string_view const string, size_t const characterLimit)
{
    if (string.size() > characterLimit)
    {
        return EscapeString(string.substr(0, characterLimit)) + "...";
    }
    else
    {
        return EscapeString(string);
    }
}

void ReplaceNewlinesIn(std::string& string, std::string_view const replacement)
{
    for (size_t pos = string.find_first_of("\r\n"); pos != std::string::npos; pos = string.find_first_of("\r\n", pos))
    {
        if (string[pos] == '\r' && pos + 1 < string.size() && string[pos + 1] == '\n')
        {
            string.replace(pos, 2, replacement);
        }
        else
        {
            string.replace(pos, 1, replacement);
        }
        pos += replacement.size();
    }
}

void ReplaceIn(std::string& string, std::string_view const placeholder, std::string_view const replacement)
{
    for (size_t pos = string.find(placeholder); pos != std::string::npos; pos = string.find(placeholder, pos))
    {
        string.replace(pos, placeholder.length(), replacement);
        pos += replacement.length();
    }
}

std::int64_t CountLines(std::string_view const text)
{
    if (text.empty())
    {
        return 0;
    }

    std::int64_t const newlineCount = static_cast<std::int64_t>(std::count(text.begin(), text.end(), '\n'));
    return text.ends_with('\n') ? newlineCount : newlineCount + 1;
}

std::vector<std::string> SplitLines(std::string_view const content)
{
    std::vector<std::string> lines;
    if (content.empty())
    {
        return lines;
    }

    std::istringstream input{std::string(content)};
    std::string line;
    while (std::getline(input, line))
    {
        lines.push_back(std::move(line));
    }

    return lines;
}

std::string JoinLines(std::vector<std::string> const& lines)
{
    if (lines.empty())
    {
        return {};
    }

    std::ostringstream output;
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        if (index > 0)
        {
            output << '\n';
        }
        output << lines[index];
    }
    return output.str();
}
