#include "PromptCommand.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <print>
#include <string>
#include <string_view>

namespace
{
    // Leading and trailing blanks, which also drops the '\r' that std::getline leaves
    // behind on CRLF input.
    constexpr std::string_view Blanks = " \t\r\n\v\f";

    constexpr std::string_view CommandNameCharacters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";

    constexpr std::string_view TrimBlanks(std::string_view text)
    {
        std::size_t const begin = text.find_first_not_of(Blanks);
        if (begin == std::string_view::npos)
        {
            return {};
        }
        std::size_t const end = text.find_last_not_of(Blanks);
        return text.substr(begin, end - begin + 1);
    }

    struct CommandEntry
    {
        std::string_view  name;
        PromptCommandKind kind;
        std::string_view  usage; // shown by PrintPromptCommands
    };

    constexpr CommandEntry Commands[] =
    {
        { "help",  PromptCommandKind::help,  "list the available commands" },
        { "bye",   PromptCommandKind::bye,   "end the session" },
    };
}

PromptCommand ParsePromptCommand(std::string_view line, bool allowCommands)
{
    std::string_view const body = TrimBlanks(line);
    if (body.empty())
    {
        return { .kind = PromptCommandKind::blank };
    }
    if (!allowCommands || body.front() != '/')
    {
        return { .kind = PromptCommandKind::prompt, .text = std::string(body) };
    }
    std::size_t const wordEnd = body.find_first_not_of(CommandNameCharacters, std::size_t{1});
    if (wordEnd == std::size_t{1})
    {
        // The '/' is followed by no name character at all, so this is prompt text.
        return { .kind = PromptCommandKind::prompt, .text = std::string(body) };
    }
    std::string_view const word = wordEnd == std::string_view::npos ? body.substr(1) : body.substr(1, wordEnd - 1);
    if (word.empty())
    {
        // A lone '/': the slash names nothing, so it is prompt text too.
        return { .kind = PromptCommandKind::prompt, .text = std::string(body) };
    }
    std::string_view const rest = wordEnd == std::string_view::npos ? std::string_view{} : TrimBlanks(body.substr(wordEnd));

    auto const found = std::ranges::find_if(Commands, [&](CommandEntry const& entry)
    {
        return entry.name == word;
    });
    if (found != std::ranges::end(Commands))
    {
        return { .kind = found->kind, .name = std::string(found->name), .argument = std::string(rest) };
    }
    return { .kind = PromptCommandKind::unknown, .name = std::string(word), .argument = std::string(rest) };
}

void PrintPromptCommands()
{
    std::println("Available commands:");
    for (CommandEntry const& entry : Commands)
    {
        std::println("  /{:<12}{}", entry.name, entry.usage);
    }
    std::println("Anything else is sent to the model as a prompt.");
}
