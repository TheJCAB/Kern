#pragma once

#include <string>
#include <string_view>

// What a single line typed at the prompt asks for. The kinds are dispatched by the
// prompt loop in main.cpp; adding a command means adding one enumerator here and one
// row to the table in PromptCommand.cpp.
enum class PromptCommandKind
{
    blank,     // the line was empty: ask for another prompt
    prompt,    // ordinary text: hand it to the session
    help,      // '/help': list the available commands
    bye,       // '/bye': end the session
    moreTurns, // '/continue [N]': grant the session more turns and resume
    unknown,   // a '/'-prefixed line whose first word names no command
};

// One line of input, split into a command and its parts.
struct PromptCommand
{
    PromptCommandKind kind = PromptCommandKind::blank;
    std::string       text{};     // prompt text for the session; set only for kind 'prompt'
    std::string       name{};     // the command word without its leading '/'; set for command kinds
    std::string       argument{}; // what follows the command word, trimmed; empty when there is none
};

// Classifies one line of input. Leading and trailing blanks are always removed,
// including the '\r' that std::getline leaves on CRLF input, so a line that is empty
// once trimmed is 'blank'. A line is a command only when it starts with '/' and the
// text up to the first space or tab (or the end of the line) is a non-empty word made
// only of letters, digits, '_' or '-'; therefore '/', '//comment', '/ hello' and
// '/src/main.cpp' are ordinary prompt text, since their first word is empty or
// contains a character outside that set. A recognised command word gives its kind and
// an unrecognised one gives 'unknown'.
PromptCommand ParsePromptCommand(std::string_view line, bool allowCommands = true);

// Prints the command list to stdout, one command per line, from the same table the
// parser uses.
void PrintPromptCommands();
