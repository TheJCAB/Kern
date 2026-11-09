#pragma once

#include "JsonUtilities.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Identifier of the running build, used to correlate transcripts with the binary that produced them.
inline constexpr std::string_view KernBuildId = __DATE__ " " __TIME__;

// Version of the transcript layout written by SessionRecorder; bumped when the manifest keys change.
inline constexpr int SessionTranscriptSchemaVersion = 1;

// Who the session is: a stable id, the session it was spawned from, its role and an optional tag.
struct SessionIdentity
{
    std::string id{};         // Unique per session; also the prefix of both artifacts.
    std::string parentId{};   // Empty for a root session.
    std::string role{ "main" };
    std::string tag{};        // Omitted from the manifest when empty.
};

// What produced the session: build, model, decoding parameters, endpoint and caps.
struct SessionProvenance
{
    std::string buildId{ KernBuildId };
    std::string modelName{};
    json        decoding{ json::object() };
    json        endpoint{ json::object() };
    unsigned    maxTokens = 0;
    unsigned    maxTurns  = 0;
};

// What was fed into the session before the first turn.
struct SessionInputs
{
    std::string systemPrompt{};                  // Stored in full; no hashes are written.
    json        toolDefinitions{ json::array() };
    std::string userPrompt{};
};

// Writes two artifacts per session into one directory: <id>_session.json (the manifest, rewritten at
// start and at end) and <id>_turns.jsonl (append-only, one JSON object per turn). Every write is best
// effort: a failure is logged as a warning, disables the recorder and never propagates.
class SessionRecorder
{
public:
    // `enabled` false makes every method a no-op; used by `--record off`.
    SessionRecorder(SessionIdentity identity,
                    SessionProvenance provenance,
                    SessionInputs inputs,
                    std::filesystem::path directory,
                    bool enabled = true);
    // Sets the decoding-parameters row of the manifest provenance. Must be called
    // before BeginSession; calls after BeginSession are ignored.
    void SetDecoding(json const& decoding);

    // Writes the manifest for the first time with "complete": false and no outcome. A second call is
    // ignored, so only the first user prompt and turn budget are recorded.
    void BeginSession(unsigned maxTurns, std::string_view userPrompt);

    // Appends one line to the jsonl file; usage counters are normalised onto the line before it is written.
    void RecordTurn(json const& turn);

    // Rewrites the manifest with "complete": true and the supplied outcome object.
    void EndSession(json const& outcome);

    // Copies prompt, completion and total token counts out of "usage" onto "promptTokens",
    // "completionTokens" and "totalTokens", omitting any count that is not known.
    static void NormaliseTokenUsage(json& turn);

    // Reads one unsigned token count; returns nothing for missing members or for values that are not
    // non-negative integers that fit an unsigned.
    static std::optional<unsigned> ReadTokenCount(json const& usage, std::string_view key);

    // Returns the first known count over the half-open range of keys, skipping empty keys.
    static std::optional<unsigned> ReadAnyTokenCount(json const& usage, std::string_view const* begin, std::string_view const* end);

    // False once a write has failed; further calls then do nothing.
    bool Enabled() const;

    // Full path of the manifest, <directory>/<id>_session.json.
    std::filesystem::path const& ManifestPath() const;

private:
    // Builds and writes the manifest atomically; returns false (and disables the recorder) on failure.
    bool WriteManifest(bool complete, json const& outcome);

    // Logs a warning naming the artifact and the error, then stops all further writes.
    void Disable(std::string_view artifact, std::string_view error);

    SessionIdentity       m_identity;
    SessionProvenance     m_provenance;
    SessionInputs         m_inputs;
    std::filesystem::path m_directory;
    std::filesystem::path m_manifestPath;
    std::filesystem::path m_turnsPath;
    bool                  m_enabled = true;
    bool                  m_started = false;
};
