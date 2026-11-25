#include "Tools/note.h"

#include "TestUtilities.h"

#include <print>
#include <string>
#include <string_view>

namespace
{

bool HasError(json const& answer, std::string_view const errorCode)
{
    return answer.contains("error") && answer.contains("errorCode") && answer["errorCode"].get<std::string>() == std::string{ errorCode };
}

} // namespace

int main()
{
    ToolsRuntimeContext const context{};
    std::string_view const    sessionId = "session";

    std::println("a note is acknowledged in place");
    {
        json const answer = NoteTool(json{ { "text", "keep this" }, { "id", "n1" } }, context, sessionId);

        ExpectEqual(answer["ok"].get<bool>(), true, "a note must be acknowledged");
        ExpectEqual(answer["id"].get<std::string>(), std::string("n1"), "a note must echo the id it was given");
        ExpectEqual(answer["chars"].get<std::size_t>(), std::size_t{ 9 }, "a note must report the size of the text it kept");
        Expect(!answer.contains("obsolete"), "a plain note must not look like an obsolete marker");
    }

    std::println("a note without text and without obsolete is refused");
    {
        json const answer = NoteTool(json::object(), context, sessionId);

        Expect(HasError(answer, "invalid_argument"), "a note with neither text nor obsolete must be refused as invalid_argument");
    }

    std::println("a note over the limit is refused");
    {
        std::string const text   = std::string(MaxNoteChars + 1, 'x');
        json const        answer = NoteTool(json{ { "text", text } }, context, sessionId);

        Expect(HasError(answer, "too_long"), "a note over MaxNoteChars must be refused as too_long");
        Expect(answer["error"].get<std::string>().find(std::to_string(MaxNoteChars)) != std::string::npos,
               "a note over the limit must name that limit in its error");
    }

    std::println("marking a note obsolete records a marker and removes nothing");
    {
        json const withReason = NoteTool(json{ { "text", "no longer applies" }, { "obsolete", "n1" } }, context, sessionId);

        ExpectEqual(withReason["ok"].get<bool>(), true, "a note marked obsolete must still be acknowledged");
        ExpectEqual(withReason["obsolete"].get<std::string>(), std::string("n1"), "an obsolete marker must name the note it retires");
        Expect(!withReason.contains("error"), "an obsolete marker with a reason must not report an error");

        json const withoutReason = NoteTool(json{ { "obsolete", "n1" } }, context, sessionId);

        ExpectEqual(withoutReason["ok"].get<bool>(), true, "an obsolete marker without text must be acknowledged");
        ExpectEqual(withoutReason["obsolete"].get<std::string>(), std::string("n1"), "an obsolete marker without text must still name the note it retires");
        Expect(!withoutReason.contains("error"), "an obsolete marker without text must not report an error");
    }

    std::println("the id and the text are both optional together");
    {
        json const answer = NoteTool(json{ { "id", "n2" } }, context, sessionId);

        Expect(HasError(answer, "invalid_argument"), "a note with only an id must be refused as invalid_argument");
    }

    return 0;
}
