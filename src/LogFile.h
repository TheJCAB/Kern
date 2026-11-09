#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// The numbered artifacts of one run, written into one directory as `<id>_<nnn>_<name>.<extension>`: the id is
// random per run and the sequence number advances once per request. Every write truncates its file, and a write
// that cannot be opened throws std::runtime_error.
class LogFile
{
public:
    // Directory that receives the artifacts: the `logs` directory beside the executable, created on demand.
    static std::filesystem::path const& Directory();

    // Uses Directory() for the artifacts and a freshly generated id.
    LogFile();

    // `directory` must exist; `id` defaults to a freshly generated one.
    explicit LogFile(std::filesystem::path directory, std::string id);

    std::string const& Id() const { return m_id; }

    // Sequence number of the artifacts written next; it starts at zero.
    int Sequence() const { return m_sequence; }

    // Advances the sequence number used by subsequent paths and writes; returns the new value.
    int Advance() { return ++m_sequence; }

    // Full path of one artifact, with the sequence number zero-padded to three digits.
    std::filesystem::path Path(std::string_view name, std::string_view extension = "json") const;

    // Writes `content` to Path(name, extension), truncating any previous file with that path.
    void Write(std::string_view name, std::string_view content, std::string_view extension = "json") const;

private:
    std::filesystem::path m_directory;
    std::string           m_id;
    int                   m_sequence = 0;
};
