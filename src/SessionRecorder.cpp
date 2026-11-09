#include "SessionRecorder.h"

#include "FileUtilities.h"
#include "Logging.h"

#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

SessionRecorder::SessionRecorder(SessionIdentity identity,
                                 SessionProvenance provenance,
                                 SessionInputs inputs,
                                 std::filesystem::path directory,
                                 bool const enabled)
    : m_identity(std::move(identity))
    , m_provenance(std::move(provenance))
    , m_inputs(std::move(inputs))
    , m_directory(std::move(directory))
    , m_manifestPath(m_directory / (m_identity.id + "_session.json"))
    , m_turnsPath(m_directory / (m_identity.id + "_turns.jsonl"))
    , m_enabled(enabled)
    , m_started(false)
{
}

bool SessionRecorder::Enabled() const
{
    return m_enabled;
}

std::filesystem::path const& SessionRecorder::ManifestPath() const
{
    return m_manifestPath;
}

void SessionRecorder::BeginSession(unsigned const maxTurns, std::string_view const userPrompt)
{
    if (!m_enabled || m_started)
    {
        return;
    }

    m_started              = true;
    m_provenance.maxTurns  = maxTurns;
    m_inputs.userPrompt    = std::string(userPrompt);
    WriteManifest(false, json::object());
}

void SessionRecorder::SetDecoding(json const& decoding)
{
    if (m_started)
    {
        return;
    }

    m_provenance.decoding = decoding;
}

void SessionRecorder::RecordTurn(json const& turn)
{
    if (!m_enabled)
    {
        return;
    }

    try
    {
        json line = turn;
        NormaliseTokenUsage(line);

        std::ofstream output(m_turnsPath, std::ios::out | std::ios::app);
        if (!output)
        {
            throw std::runtime_error{ "could not open the turns file for appending" };
        }

        output << line.dump() << '\n';
        output.flush();
        if (!output)
        {
            throw std::runtime_error{ "could not append to the turns file" };
        }
    }
    catch (std::exception const& error)
    {
        Disable(m_turnsPath.filename().string(), error.what());
    }
}

void SessionRecorder::EndSession(json const& outcome)
{
    if (!m_enabled)
    {
        return;
    }

    WriteManifest(true, outcome);
}

bool SessionRecorder::WriteManifest(bool const complete, json const& outcome)
{
    if (!m_enabled)
    {
        return false;
    }

    try
    {
        json identity = json::object();
        identity["id"]       = m_identity.id;
        identity["role"]     = m_identity.role;
        identity["parentId"] = m_identity.parentId;
        if (!m_identity.tag.empty())
        {
            identity["tag"] = m_identity.tag;
        }
        identity["sessionFile"] = m_manifestPath.filename().string();
        identity["turnsFile"]   = m_turnsPath.filename().string();

        json provenance  = json::object();
        provenance["buildId"]   = m_provenance.buildId;
        provenance["model"]     = m_provenance.modelName;
        provenance["decoding"]  = m_provenance.decoding;
        provenance["endpoint"]  = m_provenance.endpoint;
        provenance["maxTokens"] = m_provenance.maxTokens;
        provenance["maxTurns"]  = m_provenance.maxTurns;

        json inputs = json::object();
        inputs["systemPrompt"]    = m_inputs.systemPrompt;
        inputs["toolDefinitions"] = m_inputs.toolDefinitions;
        inputs["userPrompt"]      = m_inputs.userPrompt;

        json manifest = json::object();
        manifest["schemaVersion"] = SessionTranscriptSchemaVersion;
        manifest["complete"]      = complete;
        manifest["identity"]      = std::move(identity);
        manifest["provenance"]    = std::move(provenance);
        manifest["inputs"]        = std::move(inputs);
        manifest["outcome"]       = outcome;

        RawWriteTextFile(m_manifestPath, manifest.dump(2));
        return true;
    }
    catch (std::exception const& error)
    {
        Disable(m_manifestPath.filename().string(), error.what());
        return false;
    }
}

void SessionRecorder::Disable(std::string_view const artifact, std::string_view const error)
{
    m_enabled = false;
    Log(LogLevel::warning, "session recorder disabled: could not write {}: {}", artifact, error);
}

void SessionRecorder::NormaliseTokenUsage(json& turn)
{
    if (!turn.is_object())
    {
        return;
    }

    auto const usage = turn.find("usage");
    if (usage == turn.end() || !usage->is_object())
    {
        return;
    }

    static constexpr std::string_view promptKeys[]     = { "prompt_tokens", "prompt_eval_count" };
    static constexpr std::string_view completionKeys[] = { "completion_tokens", "eval_count" };

    auto const promptTokens     = ReadAnyTokenCount(*usage, std::begin(promptKeys), std::end(promptKeys));
    auto const completionTokens = ReadAnyTokenCount(*usage, std::begin(completionKeys), std::end(completionKeys));

    if (promptTokens)
    {
        turn["promptTokens"] = *promptTokens;
    }
    if (completionTokens)
    {
        turn["completionTokens"] = *completionTokens;
    }

    if (auto const totalTokens = ReadTokenCount(*usage, "total_tokens"))
    {
        turn["totalTokens"] = *totalTokens;
    }
    else if (promptTokens && completionTokens)
    {
        turn["totalTokens"] = *promptTokens + *completionTokens;
    }
}

std::optional<unsigned> SessionRecorder::ReadTokenCount(json const& usage, std::string_view const key)
{
    if (!usage.is_object())
    {
        return std::nullopt;
    }

    auto const value = usage.find(key);
    if (value == usage.end())
    {
        return std::nullopt;
    }

    if (value->is_number_unsigned())
    {
        auto const raw = value->get<nlohmann::json::number_unsigned_t>();
        if (raw > std::numeric_limits<unsigned>::max())
        {
            return std::nullopt;
        }
        return static_cast<unsigned>(raw);
    }

    if (value->is_number_integer())
    {
        auto const raw = value->get<nlohmann::json::number_integer_t>();
        if (raw < 0 || static_cast<nlohmann::json::number_unsigned_t>(raw) > std::numeric_limits<unsigned>::max())
        {
            return std::nullopt;
        }
        return static_cast<unsigned>(raw);
    }

    return std::nullopt;
}

std::optional<unsigned> SessionRecorder::ReadAnyTokenCount(json const& usage,
                                                           std::string_view const* const begin,
                                                           std::string_view const* const end)
{
    for (auto const* key = begin; key != end; ++key)
    {
        if (key->empty())
        {
            continue;
        }

        if (auto const count = ReadTokenCount(usage, *key))
        {
            return count;
        }
    }

    return std::nullopt;
}
