#include "Memory.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    // Ids are model-supplied and end up in prompt text and in list filters, so they stay to a simple alphabet.
    constexpr std::size_t MaxIdChars = 64;

    bool IsIdChar(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
               c == '-';
    }

    // Text made up only of spaces, tabs and newlines says nothing and would still cost prompt space.
    bool IsBlank(std::string const& text)
    {
        return std::ranges::all_of(text, [](char c)
                                   {
                                       return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
                                              c == '\f';
                                   });
    }

    // The characters every stored text adds up to, which is what the total budget is measured against.
    std::size_t TotalChars(std::map<std::string, MemoryEntry> const& entries)
    {
        std::size_t total = 0;
        for (MemoryEntry const& entry : entries | std::views::values)
        {
            total += entry.text.size();
        }
        return total;
    }

    json NotFound(std::string_view id)
    {
        return json{{"error", std::format("no memory entry '{}'", id)}, {"errorCode", "not_found"}};
    }

    json TooLong()
    {
        return json{{"error", std::format("entry exceeds {} characters", MemoryStore::MaxEntryChars)},
                    {"errorCode", "too_long"}};
    }

    json OverBudget()
    {
        return json{{"error",
                     std::format("store would exceed {} characters; remove or shorten an entry",
                                 MemoryStore::MaxTotalChars)},
                    {"errorCode", "full"}};
    }
}  // namespace

std::optional<MemoryEntry::Kind> MemoryStore::ParseKind(std::string_view kindName)
{
    if (kindName == "fact")
    {
        return MemoryEntry::Kind::fact;
    }
    if (kindName == "decision")
    {
        return MemoryEntry::Kind::decision;
    }
    if (kindName == "invariant")
    {
        return MemoryEntry::Kind::invariant;
    }
    if (kindName == "task")
    {
        return MemoryEntry::Kind::task;
    }
    return std::nullopt;
}

std::string_view MemoryStore::KindName(MemoryEntry::Kind kind)
{
    switch (kind)
    {
        case MemoryEntry::Kind::fact:
            return "fact";
        case MemoryEntry::Kind::decision:
            return "decision";
        case MemoryEntry::Kind::invariant:
            return "invariant";
        case MemoryEntry::Kind::task:
            return "task";
    }
    return {};  // Unreachable for a kind that came from ParseKind or from the store itself.
}

json MemoryStore::Add(std::string_view id, MemoryEntry::Kind kind, std::string const& text,
                      std::string const& createdBy)
{
    if (id.empty() || id.size() > MaxIdChars || !std::ranges::all_of(id, IsIdChar))
    {
        return {{"error", std::format("id must match [A-Za-z0-9._-] and be at most {} characters", MaxIdChars)},
                {"errorCode", "invalid_argument"}};
    }
    if (IsBlank(text))
    {
        return {{"error", "text must not be empty"}, {"errorCode", "invalid_argument"}};
    }
    if (text.size() > MaxEntryChars)
    {
        return TooLong();
    }
    if (m_entries.contains(std::string(id)))
    {
        return {{"error", std::format("memory entry '{}' already exists; use replace", id)},
                {"errorCode", "invalid_argument"}};
    }
    if (m_entries.size() >= MaxEntries)
    {
        return {{"error", std::format("store is full ({} entries); remove one first", MaxEntries)},
                {"errorCode", "full"}};
    }
    if (TotalChars(m_entries) + text.size() > MaxTotalChars)
    {
        return OverBudget();
    }

    m_entries.emplace(std::string(id),
                      MemoryEntry{
                          .id = std::string(id),
                          .kind = kind,
                          .text = text,
                          .createdBy = createdBy,
                      });

    return {{"ok", true},
            {"id", std::string(id)},
            {"kind", KindName(kind)},
            {"entries", m_entries.size()},
            {"chars_used", TotalChars(m_entries)}};
}

json MemoryStore::Replace(std::string_view id, std::string const* text, std::optional<MemoryEntry::Kind> kind)
{
    if (text == nullptr && !kind.has_value())
    {
        return {{"error", "replace needs text and/or kind"}, {"errorCode", "invalid_argument"}};
    }
    if (text != nullptr)
    {
        if (IsBlank(*text))
        {
            return {{"error", "text must not be empty"}, {"errorCode", "invalid_argument"}};
        }
        if (text->size() > MaxEntryChars)
        {
            return TooLong();
        }
    }

    auto const it = m_entries.find(std::string(id));
    if (it == m_entries.end())
    {
        return NotFound(id);
    }

    if (text != nullptr && TotalChars(m_entries) - it->second.text.size() + text->size() > MaxTotalChars)
    {
        return OverBudget();
    }

    if (text != nullptr)
    {
        it->second.text = *text;
    }
    if (kind.has_value())
    {
        it->second.kind = *kind;
    }

    json result = {{"ok", true},
                   {"id", std::string(id)},
                   {"kind", KindName(it->second.kind)},
                   {"entries", m_entries.size()}};
    if (text != nullptr)
    {
        result["chars_used"] = TotalChars(m_entries);
    }
    return result;
}

json MemoryStore::Remove(std::string_view id)
{
    auto const it = m_entries.find(std::string(id));
    if (it == m_entries.end())
    {
        return NotFound(id);
    }

    m_entries.erase(it);
    return {{"ok", true}, {"id", std::string(id)}, {"entries", m_entries.size()}};
}

MemoryStore::Listing MemoryStore::List(std::string_view idPrefix, std::size_t digestChars) const
{
    Listing listing;
    for (MemoryEntry const& entry : m_entries | std::views::values)
    {
        if (!entry.id.starts_with(idPrefix))
        {
            continue;
        }
        listing.charsUsed += entry.text.size();
        listing.items.push_back({
            {"id", entry.id},
            {"kind", KindName(entry.kind)},
            {"text", entry.text},
            {"created_by", entry.createdBy},
        });
    }

    // The digest is always the whole store, never only the entries the prefix matched: what the next turn
    // shows does not depend on what the caller happened to ask about.
    auto const [digest, emitted] = Render(digestChars);
    listing.digest = digest;
    listing.inDigest = emitted;
    return listing;
}

std::string MemoryStore::Digest(std::size_t maxChars) const
{
    if (m_entries.empty())
    {
        return {};  // Nothing remembered means nothing at all is added to the request.
    }
    return Render(maxChars).first;
}

bool MemoryStore::Empty() const
{
    return m_entries.empty();
}

std::pair<std::string, std::size_t> MemoryStore::Render(std::size_t maxChars) const
{
    std::string digest = "[Memory - entries you asked to keep. Update with the memory tool.]\n";
    std::size_t emitted = 0;
    for (MemoryEntry const& entry : m_entries | std::views::values)
    {
        std::string const line = std::format("- [{}] {}: {}\n", KindName(entry.kind), entry.id, entry.text);
        if (digest.size() + line.size() > maxChars)
        {
            break;  // Everything from here on is only counted, never rendered.
        }
        digest += line;
        ++emitted;
    }

    if (auto const dropped = m_entries.size() - emitted; dropped > 0)
    {
        digest += std::format("[+{} more; call memory(list)]\n", dropped);
    }

    return {std::move(digest), emitted};
}