#include "ToolUtilities.h"

#include "TestUtilities.h"

#include "Tools/delete_file.h"
#include "Tools/implement.h"
#include "Tools/note.h"
#include "Tools/read_file_chunk.h"
#include "Tools/research.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

namespace
{

// MainTools lives in src/main.cpp:53-60, in that file's unnamed namespace, so it is unreachable from a test TU:
// its members are named individually here, in the same order and with the same membership.
consteval auto MainToolNames() -> std::array<std::string_view, 5>
{
    return { Tools::research::Definition.name,
             Tools::implement::Definition.name,
             read_file_chunk.name,
             note.name,
             delete_file.name };
}

consteval auto MainToolDescriptions() -> std::array<std::string_view, 5>
{
    return { Tools::research::Definition.description,
             Tools::implement::Definition.description,
             read_file_chunk.description,
             note.description,
             delete_file.description };
}

// Every inspection below happens during constant evaluation (`consteval`), and each one names the real tool lists
// directly inside its own body: no array is passed in and no mirror array exists at namespace scope. odr-using a
// tool object at runtime would take the address of its callTool and drag Glob, ValidatedFileSystem, RunProcess and
// the line helpers into the link. What escapes constant evaluation is only string_views pointing at literals and bools.
consteval auto ResearchToolNames() -> std::array<std::string_view, std::size(Tools::research::Tools)>
{
    constexpr std::size_t Count = std::size(Tools::research::Tools);

    std::array<std::string_view, Count> names{};

    for (std::size_t index = 0; index < Count; ++index)
    {
        names[index] = Tools::research::Tools[index].name;
    }

    return names;
}

consteval auto ResearchToolDescriptions() -> std::array<std::string_view, std::size(Tools::research::Tools)>
{
    constexpr std::size_t Count = std::size(Tools::research::Tools);

    std::array<std::string_view, Count> descriptions{};

    for (std::size_t index = 0; index < Count; ++index)
    {
        descriptions[index] = Tools::research::Tools[index].description;
    }

    return descriptions;
}

consteval auto ImplementToolNames() -> std::array<std::string_view, std::size(Tools::implement::Tools)>
{
    constexpr std::size_t Count = std::size(Tools::implement::Tools);

    std::array<std::string_view, Count> names{};

    for (std::size_t index = 0; index < Count; ++index)
    {
        names[index] = Tools::implement::Tools[index].name;
    }

    return names;
}

consteval auto ImplementToolDescriptions() -> std::array<std::string_view, std::size(Tools::implement::Tools)>
{
    constexpr std::size_t Count = std::size(Tools::implement::Tools);

    std::array<std::string_view, Count> descriptions{};

    for (std::size_t index = 0; index < Count; ++index)
    {
        descriptions[index] = Tools::implement::Tools[index].description;
    }

    return descriptions;
}

// A tool with an empty prompt fragment is fine, because an empty string is a substring of everything.
// A non-empty fragment is a violation when it and the tool's own description contain each other verbatim.
constexpr bool RepeatsDescription(std::string_view const fragment, std::string_view const description)
{
    return !fragment.empty() &&
           (description.find(fragment) != std::string_view::npos || fragment.find(description) != std::string_view::npos);
}

consteval auto MainToolFragmentDuplication() -> std::array<bool, 5>
{
    return { RepeatsDescription(Tools::research::Definition.promptFragment, Tools::research::Definition.description),
             RepeatsDescription(Tools::implement::Definition.promptFragment, Tools::implement::Definition.description),
             RepeatsDescription(read_file_chunk.promptFragment, read_file_chunk.description),
             RepeatsDescription(note.promptFragment, note.description),
             RepeatsDescription(delete_file.promptFragment, delete_file.description) };
}

consteval auto ResearchToolFragmentDuplication() -> std::array<bool, std::size(Tools::research::Tools)>
{
    constexpr std::size_t Count = std::size(Tools::research::Tools);

    std::array<bool, Count> repeats{};

    for (std::size_t index = 0; index < Count; ++index)
    {
        repeats[index] = RepeatsDescription(Tools::research::Tools[index].promptFragment, Tools::research::Tools[index].description);
    }

    return repeats;
}

consteval auto ImplementToolFragmentDuplication() -> std::array<bool, std::size(Tools::implement::Tools)>
{
    constexpr std::size_t Count = std::size(Tools::implement::Tools);

    std::array<bool, Count> repeats{};

    for (std::size_t index = 0; index < Count; ++index)
    {
        repeats[index] = RepeatsDescription(Tools::implement::Tools[index].promptFragment, Tools::implement::Tools[index].description);
    }

    return repeats;
}

constexpr auto mainToolNames = MainToolNames();
constexpr auto mainToolDescriptions = MainToolDescriptions();
constexpr auto researchToolNames = ResearchToolNames();
constexpr auto researchToolDescriptions = ResearchToolDescriptions();
constexpr auto implementToolNames = ImplementToolNames();
constexpr auto implementToolDescriptions = ImplementToolDescriptions();
constexpr auto mainToolFragmentDuplication = MainToolFragmentDuplication();
constexpr auto researchToolFragmentDuplication = ResearchToolFragmentDuplication();
constexpr auto implementToolFragmentDuplication = ImplementToolFragmentDuplication();

consteval auto AllNonEmpty(std::ranges::range auto const& values) -> bool
{
    return std::ranges::all_of(values, [](std::string_view const value) { return !value.empty(); });
}

consteval auto NoneTrue(std::ranges::range auto const& values) -> bool
{
    return std::ranges::all_of(values, [](bool const value) { return !value; });
}

json NoOpArguments(json const&, ToolsRuntimeContext const&, std::string_view const)
{
    return json::object();
}

constexpr ToolDefinition FragmentATool
{
    .name           = "fragment_a",
    .description    = "The first of two tools used to pin the paragraph layout of prompt fragments.",
    .callTool       = NoOpArguments,
    .promptFragment = "A",
};

constexpr ToolDefinition FragmentBTool
{
    .name           = "fragment_b",
    .description    = "The second of two tools used to pin the paragraph layout of prompt fragments.",
    .callTool       = NoOpArguments,
    .promptFragment = "B",
};

constexpr ToolDefinition NoFragmentFirstTool
{
    .name        = "no_fragment_first",
    .description = "A tool without standing policy of its own.",
    .callTool    = NoOpArguments,
};

constexpr ToolDefinition NoFragmentSecondTool
{
    .name        = "no_fragment_second",
    .description = "Another tool without standing policy of its own.",
    .callTool    = NoOpArguments,
};

constexpr ToolDefinition NoFragmentTools[]{ NoFragmentFirstTool, NoFragmentSecondTool };

constexpr std::array<ToolDefinition, 0> NoTools{};

constexpr ToolDefinition TwoFragmentTools[]{ FragmentATool, FragmentBTool };

constexpr ToolDefinition OneFragmentTool[]{ FragmentATool };

} // namespace

int main()
{
    // The checks below run during constant evaluation and keep their section markers as printed progress notes. Reading
    // a real tool object at runtime would odr-use it and pull Glob, ValidatedFileSystem, RunProcess and the line helpers
    // into the link, so the same rules are asserted on the `consteval` mirror predicates instead.
    std::println("every main tool has a name and a description");
    {
        static_assert(AllNonEmpty(mainToolNames), "every main tool must have a name");
        static_assert(AllNonEmpty(mainToolDescriptions), "every main tool must have a description");
    }

    std::println("every subagent tool has a name and a description");
    {
        static_assert(AllNonEmpty(researchToolNames), "every research tool must have a name");
        static_assert(AllNonEmpty(researchToolDescriptions), "every research tool must have a description");
        static_assert(AllNonEmpty(implementToolNames), "every implement tool must have a name");
        static_assert(AllNonEmpty(implementToolDescriptions), "every implement tool must have a description");
    }

    std::println("no tool repeats its prompt fragment in its own description");
    {
        static_assert(NoneTrue(mainToolFragmentDuplication), "a tool's prompt fragment must not repeat text from its own description");
        static_assert(NoneTrue(researchToolFragmentDuplication), "a tool's prompt fragment must not repeat text from its own description");
        static_assert(NoneTrue(implementToolFragmentDuplication), "a tool's prompt fragment must not repeat text from its own description");
    }

    std::println("fragments of tools without one are not emitted");
    {
        std::string const fromToolsWithoutFragment = BuildToolPromptFragments(NoFragmentTools);
        std::string const fromEmptySpan            = BuildToolPromptFragments(std::span<ToolDefinition const>{});
        std::string const fromEmptyArray           = BuildToolPromptFragments(std::span<ToolDefinition const>{ NoTools });

        ExpectEqual(fromToolsWithoutFragment, std::string{}, "a list whose tools have no fragment must contribute no text");
        ExpectEqual(fromEmptySpan, std::string{}, "an empty span of tools must contribute no text");
        ExpectEqual(fromEmptyArray, std::string{}, "an empty array of tools must contribute no text");
    }

    std::println("each prompt fragment is emitted as its own paragraph");
    {
        std::string const twoFragments = BuildToolPromptFragments(TwoFragmentTools);
        std::string const oneFragment  = BuildToolPromptFragments(OneFragmentTool);

        ExpectEqual(twoFragments, std::string{ "\nA\n\nB\n" }, "two prompt fragments must be emitted as two paragraphs");
        ExpectEqual(oneFragment, std::string{ "\nA\n" }, "one prompt fragment must be emitted as one paragraph");
    }

    return 0;
}
