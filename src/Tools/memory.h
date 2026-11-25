#pragma once

#include <JsonUtilities.h>
#include <Memory.h>
#include <ToolUtilities.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

inline json MemoryTool(json const& arguments, ToolsRuntimeContext const& context, std::string_view parentSessionId)
{
    if (!context.memory)
    {
        return { { "error", "this session has no memory store" }, { "errorCode", "unavailable" } };
    }

    std::string const operation = GetOrDefault(arguments, "operation", std::string{});

    // A kind the model named that the store does not accept: reported rather than guessed at.
    auto const badKind = [](std::string const& kindName)
    {
        return json{ { "error", std::format("unknown kind '{}'; expected fact, decision, invariant or task", kindName) },
                     { "errorCode", "invalid_argument" } };
    };

    if (operation == "add")
    {
        std::string const id = GetOrDefault(arguments, "id", std::string{});
        std::string const text = GetOrDefault(arguments, "text", std::string{});
        if (id.empty() || text.empty())
        {
            return { { "error", "add needs an id and the text to remember" }, { "errorCode", "invalid_argument" } };
        }

        std::string const kindName = GetOrDefault(arguments, "kind", std::string{ "fact" });
        auto const kind = MemoryStore::ParseKind(kindName);
        if (!kind.has_value())
        {
            return badKind(kindName);
        }

        return context.memory->Add(id, *kind, text, std::string(parentSessionId));
    }

    if (operation == "replace")
    {
        std::string const id = GetOrDefault(arguments, "id", std::string{});
        if (id.empty())
        {
            return { { "error", "replace needs an id" }, { "errorCode", "invalid_argument" } };
        }

        std::string const text = GetOrDefault(arguments, "text", std::string{});
        std::string const kindName = GetOrDefault(arguments, "kind", std::string{});

        std::optional<MemoryEntry::Kind> kind;
        if (!kindName.empty())
        {
            kind = MemoryStore::ParseKind(kindName);
            if (!kind.has_value())
            {
                return badKind(kindName);
            }
        }

        // An empty text means the model only wants to change the kind, and the other way round.
        std::string const* const newText = text.empty() ? nullptr : &text;
        if (newText == nullptr && !kind.has_value())
        {
            return { { "error", "replace needs new text and/or kind" }, { "errorCode", "invalid_argument" } };
        }

        return context.memory->Replace(id, newText, kind);
    }

    if (operation == "remove")
    {
        std::string const id = GetOrDefault(arguments, "id", std::string{});
        if (id.empty())
        {
            return { { "error", "remove needs an id" }, { "errorCode", "invalid_argument" } };
        }

        return context.memory->Remove(id);
    }

    if (operation == "list")
    {
        std::size_t digestChars = MemoryStore::DefaultDigestChars;
        if (arguments.contains("maxChars"))
        {
            auto const requested = GetOrDefault(arguments, "maxChars", std::int64_t{});
            if (requested < static_cast<std::int64_t>(MemoryStore::MinDigestChars)
                || requested > static_cast<std::int64_t>(MemoryStore::MaxDigestChars))
            {
                return { { "error", std::format("maxChars must be between {} and {} characters",
                                                MemoryStore::MinDigestChars, MemoryStore::MaxDigestChars) },
                         { "errorCode", "invalid_argument" } };
            }
            digestChars = static_cast<std::size_t>(requested);
        }

        auto const listing = context.memory->List(GetOrDefault(arguments, "id", std::string{}), digestChars);
        // The digest is shown here as well, so that the model can see what the next turn will actually
        // carry rather than guessing from the entry count.
        return json{ { "ok", true },
                     { "entries", listing.items.size() },
                     { "chars_used", listing.charsUsed },
                     { "in_digest", listing.inDigest },
                     { "items", std::move(listing.items) },
                     { "digest", std::move(listing.digest) } };
    }

    return { { "error", std::format("unknown operation '{}'; expected add, replace, remove or list", operation) },
             { "errorCode", "invalid_argument" } };
}

constexpr ToolParameter MemoryToolParameters[] =
{
    StringToolParameter{ "operation", "What to do: 'add' a new entry, 'replace' the text and/or the kind of an "
                                      "existing id, 'remove' an id, or 'list' the entries." },
};

constexpr ToolParameter MemoryToolOptionalParameters[] =
{
    StringToolParameter{ "id", "The short stable id of an entry, e.g. 'digest-is-transient'. Required by add, "
                               "replace and remove; for list it is an optional prefix filter." },
    StringToolParameter{ "text", "What to keep, in one or two sentences at most. Required by add; for replace it "
                                 "is the new text of the entry." },
    StringToolParameter{ "kind", "One of 'fact' (the default), 'decision', 'invariant' or 'task'." },
    IntegerToolParameter{ { "maxChars", "The character budget of the digest reported by 'list'. Defaults to 2000; between 200 and 20000." },
                          std::optional<std::int64_t>{ 200 }, std::optional<std::int64_t>{ 20000 } },
};

constexpr ToolDefinition memory
{
    .name               = "memory",
    .description        = "Keep a small curated store of the decisions, invariants, facts and open tasks of this "
                          "session. Every entry is shown to you again automatically on each turn, so add only what "
                          "you would otherwise have to re-derive, and replace or remove an entry as soon as it stops "
                          "being true. add takes an id, a text and an optional kind; replace changes the text and/or "
                          "the kind of an existing id; remove deletes an id; list reports the entries, optionally "
                          "filtered by an id prefix. The store is shared with every subagent this session starts, and "
                          "it holds at most 40 entries of at most 500 characters each, 8000 characters in all: a call "
                          "that would exceed a cap is refused with errorCode 'full' or 'too_long' rather than stored. "
                          "Do not store file contents or anything a search or read tool can look up again.",
    .requiredParameters = MemoryToolParameters,
    .optionalParameters = MemoryToolOptionalParameters,
    .callTool           = MemoryTool,
};
