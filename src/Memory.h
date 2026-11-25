#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using json = nlohmann::json;

// A small, curated store of the decisions, invariants, facts and tasks that a session has decided to keep.
// It lives in memory only: nothing is written to disk and nothing is read back, so a store starts empty in
// every process and each session is responsible for populating it again.
struct MemoryEntry
{
    std::string id;
    enum class Kind { fact, decision, invariant, task } kind = Kind::fact;
    std::string text;
    std::string createdBy;  // the id of the session that added the entry
};

// The entries of one session tree, shared between a session and its subagents through a
// std::shared_ptr<MemoryStore>, so that what one of them records is visible to all of them.
// There is no locking: a session runs its tool calls synchronously and one store belongs to one session tree.
class MemoryStore
{
public:
    static constexpr std::size_t MaxEntries = 40;           // hard cap on the number of entries
    static constexpr std::size_t MaxEntryChars = 500;       // hard cap on one entry's text
    static constexpr std::size_t MaxTotalChars = 8000;      // hard cap on the sum of all texts
    static constexpr std::size_t DefaultDigestChars = 2000; // the budget of the resident digest
    static constexpr std::size_t MinDigestChars = 200;      // the smallest digest budget the tool accepts
    static constexpr std::size_t MaxDigestChars = 20000;    // the largest digest budget the tool accepts

    // What `List` answers: every matching entry with its full text, plus the digest that the next turn shows
    // and how many entries that digest had to leave out.
    struct Listing
    {
        json items{};              // id-ordered objects carrying id, kind, text and created_by
        std::size_t charsUsed = 0; // characters of stored text among the matching entries
        std::size_t inDigest = 0;  // how many entries the digest of `digestChars` shows
        std::string digest{};      // exactly the text that the next turn shows
    };

    // "fact", "decision", "invariant" or "task"; nullopt for anything else, so that a bad kind coming from the
    // model is reported rather than guessed at.
    static std::optional<MemoryEntry::Kind> ParseKind(std::string_view kindName);
    static std::string_view KindName(MemoryEntry::Kind kind);

    // Every operation answers with a JSON object and never throws for input the model got wrong: on success the
    // object carries "ok", on failure it carries "error" and "errorCode".
    json Add(std::string_view id, MemoryEntry::Kind kind, std::string const& text, std::string const& createdBy);
    json Replace(std::string_view id, std::string const* text, std::optional<MemoryEntry::Kind> kind);
    json Remove(std::string_view id);
    // `idPrefix` keeps only the entries whose id starts with it; an empty prefix keeps all of them.
    Listing List(std::string_view idPrefix, std::size_t digestChars = DefaultDigestChars) const;
    // The text the store adds to the next request, clipped to `maxChars`; empty when the store is empty, so
    // that nothing is added while there is nothing to say.
    std::string Digest(std::size_t maxChars = DefaultDigestChars) const;
    bool Empty() const;

private:
    std::map<std::string, MemoryEntry> m_entries;  // ordered => deterministic, id-sorted digest

    // The digest text and how many entries it shows. The resident digest and `list` must agree about what was
    // left out, so both answer from this one rendering.
    std::pair<std::string, std::size_t> Render(std::size_t maxChars) const;
};