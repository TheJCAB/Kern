
#include "StringUtilities.h"

#if _WIN32
#include <windows.h>
#endif

#include <cstdlib>

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
