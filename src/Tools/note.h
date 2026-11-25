#pragma once

#include <JsonUtilities.h>
#include <ToolUtilities.h>

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

// One note must fit in a response without swallowing the context, so a longer thought is meant to be split.
constexpr std::size_t MaxNoteChars = 2000;

// This tool keeps no state at all: no store, no map, no counters.
// The call's arguments and this result are part of the history at exactly this position and are replayed in order.
// A later call passing `obsolete` only records that a thought is no longer relevant and removes nothing, because
// dropping notes is the job of compaction. Nothing here is shared with subagents.
inline json NoteTool(json const& arguments, ToolsRuntimeContext const& /* context */, std::string_view /* parentSessionId */)
{
    std::string const text     = GetOrDefault(arguments, "text", std::string{});
    std::string const id       = GetOrDefault(arguments, "id", std::string{});
    std::string const obsolete = GetOrDefault(arguments, "obsolete", std::string{});

    if (text.empty() && obsolete.empty())
    {
        return { { "error", "text is required: the thought to keep, or obsolete naming a note to retire" },
                 { "errorCode", "invalid_argument" } };
    }

    if (text.size() > MaxNoteChars)
    {
        return { { "error", std::format("text is {} characters, over the limit of MaxNoteChars = {}; split it into "
                                        "one note per thought",
                                        text.size(), MaxNoteChars) },
                 { "errorCode", "too_long" } };
    }

    if (!obsolete.empty())
    {
        return { { "ok", true },
                 { "obsolete", obsolete },
                 { "detail", "recorded in place; nothing was removed from the conversation" } };
    }

    return { { "ok", true },
             { "id", id },
             { "chars", text.size() },
             { "detail", "kept in place; it stays in this turn of the conversation and needs no repetition" } };
}

constexpr ToolParameter NoteToolParameters[] =
{
    StringToolParameter{ "text", "The thought to keep, in one or two sentences (a decision and why, an invariant the "
                                 "code must keep, a constraint the user stated, an approach that failed). For a call "
                                 "that marks an earlier note obsolete, this may carry the brief reason it no longer "
                                 "applies." },
};

constexpr ToolParameter NoteToolOptionalParameters[] =
{
    StringToolParameter{ "id", "A short stable id for this note, so that a later response can mark it obsolete; a note "
                               "without an id can never be retired." },
    StringToolParameter{ "obsolete", "The id of an earlier note that is no longer relevant to the remaining work. "
                                     "This only records that fact in the conversation; nothing is removed. "
                                     "The text argument may carry the brief reason." },
};

constexpr ToolDefinition note
{
    .name               = "note",
    .description        = "Keep a thought where you thought it. It is stored in this turn of the conversation and "
                          "replayed in order afterwards. Give it an id when a later turn may retire it, and pass "
                          "obsolete with that ID to record that it no longer applies, which removes nothing from "
                          "the conversation. Keep text to one or two sentences: a longer thought is meant to be "
                          "split into one note per thought.",
    .requiredParameters = NoteToolParameters,
    .optionalParameters = NoteToolOptionalParameters,
    .callTool           = NoteTool,
    .promptFragment     = "Keep notes rare: every note is replayed for the rest of the session and costs context "
                          "each time, so never repeat one and never note anything a search or read tool can look "
                          "up again. A note is not progress narration. Notes belong to the session that made them "
                          "and are never shared with another session, so anything a subagent needs must be in the "
                          "prompt that starts it.",
};
