#include "PromptCommand.h"

#include "TestUtilities.h"

#include <print>
#include <string>
#include <string_view>

namespace
{

PromptCommand Parse(std::string_view const line) { return ParsePromptCommand(line); }

} // namespace

int main()
{
    std::println("Test Blank");
    {
        ExpectEqual(Parse("").kind, PromptCommandKind::blank, "an empty line is blank");
        ExpectEqual(Parse("   ").kind, PromptCommandKind::blank, "a line of blanks is blank");
        ExpectEqual(Parse("\t").kind, PromptCommandKind::blank, "a tab alone is blank");
        ExpectEqual(Parse("\r").kind, PromptCommandKind::blank, "a carriage return alone is blank");
    }

    std::println("Test Default");
    {
        ExpectEqual(PromptCommand{}.kind, PromptCommandKind::blank, "a default-constructed command asks for the next line");
    }

    std::println("Test Prompt");
    {
        auto const hello = Parse("hello");
        ExpectEqual(hello.kind, PromptCommandKind::prompt, "a plain word is prompt text");
        ExpectEqual(hello.text, std::string("hello"), "a plain word keeps its text");
        ExpectEqual(hello.name, std::string(""), "a prompt command has no name");

        ExpectEqual(Parse("  hello there  ").text, std::string("hello there"), "a sentence is trimmed to its words");

        auto const slash = Parse("/");
        ExpectEqual(slash.kind, PromptCommandKind::prompt, "a lone slash is prompt text");
        ExpectEqual(slash.text, std::string("/"), "a lone slash keeps its text");

        auto const comment = Parse("//comment");
        ExpectEqual(comment.kind, PromptCommandKind::prompt, "a second slash is not a name character");
        ExpectEqual(comment.text, std::string("//comment"), "a double slash line keeps its text");

        auto const blank = Parse("/ hello");
        ExpectEqual(blank.kind, PromptCommandKind::prompt, "a blank right after the slash makes prompt text");
        ExpectEqual(blank.text, std::string("/ hello"), "a blank after the slash keeps the whole line");

        auto const question = Parse("/?");
        ExpectEqual(question.kind, PromptCommandKind::prompt, "a punctuation mark right after the slash is prompt text");
        ExpectEqual(question.text, std::string("/?"), "a question mark line keeps its text");
    }

    std::println("Test Commands");
    {
        auto const help = Parse("/help");
        ExpectEqual(help.kind, PromptCommandKind::help, "/help is the help command");
        ExpectEqual(help.name, std::string("help"), "/help names help");
        ExpectEqual(help.argument, std::string(""), "/help takes no argument");

        ExpectEqual(Parse("  /help  ").kind, PromptCommandKind::help, "/help is recognised through surrounding blanks");

        auto const bye = Parse("/bye");
        ExpectEqual(bye.kind, PromptCommandKind::bye, "/bye is the bye command");
        ExpectEqual(bye.name, std::string("bye"), "/bye names bye");

        auto const upper = Parse("/HELP");
        ExpectEqual(upper.kind, PromptCommandKind::unknown, "/HELP is unknown because command words are case-sensitive");
        ExpectEqual(upper.name, std::string("HELP"), "/HELP keeps the word as it was typed");

        // A hyphen or an underscore belongs to the word, so the word is 'help-me' or
        // 'help_me', which names no command.
        auto const hyphen = Parse("/help-me");
        ExpectEqual(hyphen.kind, PromptCommandKind::unknown, "/help-me is unknown because the hyphen is part of the word");
        ExpectEqual(hyphen.name, std::string("help-me"), "/help-me names the whole hyphenated word");

        auto const underscore = Parse("/help_me");
        ExpectEqual(underscore.kind, PromptCommandKind::unknown, "/help_me is unknown because the underscore is part of the word");
        ExpectEqual(underscore.name, std::string("help_me"), "/help_me names the whole underscored word");

        auto const verbose = Parse("/help me please");
        ExpectEqual(verbose.kind, PromptCommandKind::help, "/help takes what follows its word");
        ExpectEqual(verbose.argument, std::string("me please"), "the argument after /help is kept in full");

        auto const rush = Parse("/bye  now  ");
        ExpectEqual(rush.kind, PromptCommandKind::bye, "/bye is recognised before its argument");
        ExpectEqual(rush.argument, std::string("now"), "the argument after /bye is trimmed");

        auto const tab = Parse("/bye\t");
        ExpectEqual(tab.kind, PromptCommandKind::bye, "/bye is recognised before a trailing tab");
        ExpectEqual(tab.argument, std::string(""), "a trailing tab leaves no argument");

        ExpectEqual(Parse("/bye\r").kind, PromptCommandKind::bye, "a carriage return left by CRLF input is trimmed");
    }

    std::println("Test Unknown");
    {
        auto const foo = Parse("/foo bar");
        ExpectEqual(foo.kind, PromptCommandKind::unknown, "/foo names no command");
        ExpectEqual(foo.name, std::string("foo"), "/foo names foo");
        ExpectEqual(foo.argument, std::string("bar"), "/foo keeps its argument");

        auto const letter = Parse("/x");
        ExpectEqual(letter.kind, PromptCommandKind::unknown, "/x names a one letter command that does not exist");
        ExpectEqual(letter.name, std::string("x"), "/x names x");
        ExpectEqual(letter.argument, std::string(""), "/x takes no argument");

        auto const dash = Parse("/-x");
        ExpectEqual(dash.kind, PromptCommandKind::unknown, "/-x names no command");
        ExpectEqual(dash.name, std::string("-x"), "a hyphen is part of the command name");

        auto const digit = Parse("/9");
        ExpectEqual(digit.kind, PromptCommandKind::unknown, "/9 names no command");
        ExpectEqual(digit.name, std::string("9"), "a digit is part of the command name");

        // A lone hyphen is a name all by itself, so the trailing blank is simply
        // trimmed and the line names the command '-'.
        auto const lone = Parse("/- ");
        ExpectEqual(lone.kind, PromptCommandKind::unknown, "/- names a lone hyphen command that does not exist");
        ExpectEqual(lone.name, std::string("-"), "a lone hyphen is the command name");
        ExpectEqual(lone.argument, std::string(""), "the blank after a lone hyphen is trimmed away");
    }

    std::println("Test DisabledCommands");
    {
        auto const bye = ParsePromptCommand("/bye", false);
        ExpectEqual(bye.kind, PromptCommandKind::prompt, "a slash line is prompt text when commands are disabled");
        ExpectEqual(bye.text, std::string("/bye"), "a slash line keeps its text when commands are disabled");

        auto const help = ParsePromptCommand("/help", false);
        ExpectEqual(help.kind, PromptCommandKind::prompt, "the help command is prompt text when commands are disabled");
        ExpectEqual(help.text, std::string("/help"), "the help line keeps its text when commands are disabled");

        ExpectEqual(ParsePromptCommand("  hello  ", false).text, std::string("hello"), "blanks are still trimmed when commands are disabled");
        ExpectEqual(ParsePromptCommand("", false).kind, PromptCommandKind::blank, "an empty line is still blank when commands are disabled");
    }

    std::println("All tests passed!");
    return 0;
}
