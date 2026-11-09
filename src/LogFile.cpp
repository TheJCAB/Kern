#include "LogFile.h"

#include "FileUtilities.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

std::filesystem::path const& LogFile::Directory()
{
    static auto const path = GetExecutableDirectory() / "logs";
    if (!std::filesystem::exists(path))
    {
        std::filesystem::create_directory(path);
    }
    return path;
}

LogFile::LogFile() : m_directory{ Directory() }
{
}

LogFile::LogFile(std::filesystem::path directory, std::string id)
    : m_directory{ std::move(directory) }
    , m_id       { std::move(id)        }
{
}

std::filesystem::path LogFile::Path(std::string_view const name, std::string_view const extension) const
{
    std::ostringstream filename;
    filename << m_id << '_' << std::setw(3) << std::setfill('0') << m_sequence << '_' << name << '.' << extension;
    return m_directory / filename.str();
}

void LogFile::Write(std::string_view const name, std::string_view const content, std::string_view const extension) const
{
    auto const logPath = Path(name, extension);
    if (auto output = std::ofstream(logPath, std::ios::binary | std::ios::trunc))
    {
        output << content;
    }
    else
    {
        throw std::runtime_error(std::string("unable to write log file: ") + logPath.string());
    }
}
