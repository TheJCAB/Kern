
#include "StringUtilities.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace
{
#if defined(_WIN32)
std::string ConvertEncoding(std::string_view const input, unsigned int const fromCodePage, unsigned int const toCodePage)
{
    if (input.empty())
    {
        return {};
    }

    int const wideLength = MultiByteToWideChar(
        fromCodePage,
        MB_ERR_INVALID_CHARS,
        input.data(),
        static_cast<int>(input.size()),
        nullptr,
        0);
    if (wideLength <= 0)
    {
        return std::string(input);
    }

    std::wstring wide(wideLength, L'\0');
    MultiByteToWideChar(
        fromCodePage,
        MB_ERR_INVALID_CHARS,
        input.data(),
        static_cast<int>(input.size()),
        wide.data(),
        wideLength);

    int const byteLength = WideCharToMultiByte(
        toCodePage,
        0,
        wide.data(),
        wideLength,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (byteLength <= 0)
    {
        return std::string(input);
    }

    std::string output(byteLength, '\0');
    WideCharToMultiByte(
        toCodePage,
        0,
        wide.data(),
        wideLength,
        output.data(),
        byteLength,
        nullptr,
        nullptr);
    return output;
}
#endif
}

void ConfigureConsoleForUtf8()
{
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

std::string Utf8ToSystemEncoding(std::string_view const input)
{
#if defined(_WIN32)
    unsigned int const outputCodePage = GetConsoleOutputCP();
    if (outputCodePage == CP_UTF8)
    {
        return std::string(input);
    }

    return ConvertEncoding(input, CP_UTF8, outputCodePage);
#else
    return std::string(input);
#endif
}

std::string SystemEncodingToUtf8(std::string_view const input)
{
#if defined(_WIN32)
    unsigned int const inputCodePage = GetACP();
    if (inputCodePage == CP_UTF8)
    {
        return std::string(input);
    }

    return ConvertEncoding(input, inputCodePage, CP_UTF8);
#else
    return std::string(input);
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
