#include "Memory.h"

#include "TestUtilities.h"

#include <cstddef>
#include <format>
#include <print>
#include <string>
#include <string_view>

namespace
{

// Every failure answer carries the same two keys, so only the code is worth asserting on here.
bool HasError(json const& answer, std::string_view const errorCode)
{
    return answer.contains("error") && answer.contains("errorCode")
        && answer["errorCode"].get<std::string>() == std::string{ errorCode };
}

}  // namespace

int main()
{
    std::println("Test that an empty store adds nothing to the request");
    {
        MemoryStore store;
        Expect(store.Empty(), "a new store is empty");
        ExpectEqual(store.Digest(), std::string{}, "an empty store renders no digest at all");
    }

    std::println("Test add, list, replace and remove round-trip");
    {
        MemoryStore store;
        json const added = store.Add("d-digest", MemoryEntry::Kind::decision, "The digest is transient.", "main");
        Expect(added.contains("ok"), "a successful add answers with ok");
        ExpectEqual(added["entries"].get<std::size_t>(), std::size_t{ 1 }, "one entry after the add");
        Expect(!store.Empty(), "the store is no longer empty");

        auto const listing = store.List({});
        ExpectEqual(listing.items.size(), std::size_t{ 1 }, "list reports the entry");
        ExpectEqual(listing.items[0]["text"].get<std::string>(), std::string{ "The digest is transient." },
            "list carries the full text");
        ExpectEqual(listing.items[0]["created_by"].get<std::string>(), std::string{ "main" },
            "list says which session wrote the entry");

        json const kindOnly = store.Replace("d-digest", nullptr, MemoryEntry::Kind::invariant);
        Expect(kindOnly.contains("ok"), "changing only the kind succeeds");
        Expect(!kindOnly.contains("chars_used"), "changing only the kind changes no characters");
        ExpectEqual(store.Digest(),
            std::string{ "[Memory - entries you asked to keep. Update with the memory tool.]\n"
                         "- [invariant] d-digest: The digest is transient.\n" },
            "the digest names the kind the entry has now");

        std::string const newText = "The digest is the last transient message of the request.";
        Expect(store.Replace("d-digest", &newText, std::nullopt).contains("ok"), "replacing the text succeeds");

        json const removed = store.Remove("d-digest");
        Expect(removed.contains("ok"), "remove answers with ok");
        ExpectEqual(removed["entries"].get<std::size_t>(), std::size_t{ 0 }, "no entry is left after the remove");
        Expect(store.Empty(), "removing the only entry empties the store");
        Expect(HasError(store.Remove("d-digest"), "not_found"), "removing an absent id is reported");
    }

    std::println("Test that a second add of the same id changes nothing");
    {
        MemoryStore store;
        Expect(store.Add("a-first", MemoryEntry::Kind::fact, "first text", "main").contains("ok"), "the first add works");
        json const again = store.Add("a-first", MemoryEntry::Kind::task, "second text", "main");
        Expect(HasError(again, "invalid_argument"), "a duplicate id is refused rather than overwriting");
        Expect(store.Digest().find("first text") != std::string::npos, "the entry that was already there is unchanged");
        Expect(store.Digest().find("second text") == std::string::npos, "the refused add stored nothing");
    }

    std::println("Test that replacing an absent id changes nothing");
    {
        MemoryStore store;
        Expect(HasError(store.Replace("missing", nullptr, MemoryEntry::Kind::task), "not_found"),
            "an absent id is reported");
        ExpectEqual(store.List({}).items.size(), std::size_t{ 0 }, "the store is still empty");
    }

    std::println("Test that each cap refuses an entry rather than evicting one");
    {
        MemoryStore chars;
        std::string const big(MemoryStore::MaxEntryChars, 'y');
        for (std::size_t index = 0; index < 16; ++index)
        {
            Expect(chars.Add(std::format("e-{:02}", index), MemoryEntry::Kind::fact, big, "main").contains("ok"),
                "every 500-character entry up to the total budget is stored");
        }
        json const overflow = chars.Add("e-over", MemoryEntry::Kind::fact, big, "main");
        Expect(HasError(overflow, "full"), "the entry that would pass the total budget is refused");
        Expect(overflow["error"].get<std::string>().find("8000") != std::string::npos,
            "the refusal names the budget it hit");

        MemoryStore entries;
        for (std::size_t index = 0; index < MemoryStore::MaxEntries; ++index)
        {
            Expect(entries.Add(std::format("f-{:02}", index), MemoryEntry::Kind::fact, "one", "main").contains("ok"),
                "every entry up to the count cap is stored");
        }
        Expect(HasError(entries.Add("f-over", MemoryEntry::Kind::fact, "one", "main"), "full"), "one more entry is refused");
    }

    std::println("Test that the digest is a pure function of the store and clips to its budget");
    {
        MemoryStore store;
        Expect(store.Add("i-invariant", MemoryEntry::Kind::invariant,
            "A very long invariant line that exists only to push the second entry past the digest budget.", "main")
            .contains("ok"), "the invariant is stored");
        Expect(store.Add("d-decision", MemoryEntry::Kind::decision,
            "A very long decision line that exists only to be clipped away by a small digest budget.", "research")
            .contains("ok"), "the decision is stored");

        std::string const full = store.Digest();
        Expect(full.find("- [decision] d-decision:") < full.find("- [invariant] i-invariant:"),
            "the digest is ordered by id, not by the order the entries were added");
        ExpectEqual(store.Digest(), full, "the same store renders byte-identically again");
        Expect(full.find("more; call memory(list)") == std::string::npos,
            "a digest with room left says nothing about clipping");

        std::string const clipped = store.Digest(MemoryStore::MinDigestChars);
        Expect(clipped.starts_with("[Memory - entries you asked to keep."), "a clipped digest keeps the header");
        Expect(clipped.find("more; call memory(list)") != std::string::npos,
            "a clipped digest says that entries were left out");

        auto const wide = store.List({}, MemoryStore::MaxDigestChars);
        ExpectEqual(wide.items.size(), std::size_t{ 2 }, "list reports every entry however long it is");
        ExpectEqual(wide.inDigest, std::size_t{ 2 }, "the bigger budget shows the whole store");

        auto const tight = store.List({}, MemoryStore::MinDigestChars);
        ExpectEqual(tight.items.size(), std::size_t{ 2 }, "list never loses an entry to the digest budget");
        Expect(tight.inDigest < 2, "the tight digest leaves one entry out of the prompt");
        ExpectEqual(tight.digest, clipped, "list reports exactly the digest that the next turn will carry");
    }

    std::println("Test the arguments the model can get wrong");
    {
        MemoryStore store;
        Expect(store.Add("keep-me", MemoryEntry::Kind::fact, "one fact", "main").contains("ok"), "a good add works");
        Expect(HasError(store.Add("", MemoryEntry::Kind::fact, "text", "main"), "invalid_argument"),
            "an empty id is refused");
        Expect(HasError(store.Add("bad id!", MemoryEntry::Kind::fact, "text", "main"), "invalid_argument"),
            "an id outside the simple alphabet is refused");
        Expect(HasError(store.Add("ok-id", MemoryEntry::Kind::fact, "  \n ", "main"), "invalid_argument"),
            "a text of nothing but whitespace is refused");
        std::string const huge(MemoryStore::MaxEntryChars + 1, 'z');
        Expect(HasError(store.Add("ok-huge", MemoryEntry::Kind::fact, huge, "main"), "too_long"),
            "an over-long text is refused as too_long");
        ExpectEqual(store.List({}).items.size(), std::size_t{ 1 }, "none of the bad calls stored anything");
        Expect(!MemoryStore::ParseKind("todo").has_value(), "an unknown kind name is reported rather than guessed at");
        ExpectEqual(std::string(MemoryStore::KindName(MemoryEntry::Kind::task)), std::string{ "task" },
            "each kind has exactly one name");
    }

    return 0;
}
