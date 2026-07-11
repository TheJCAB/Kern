
#include "FileUtilities.h"

#if _WIN32
#include <Windows.h>
#endif

#include <exception>
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>

std::string ReadTextFile(std::filesystem::path const& path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error{ "error: could not open file" };
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

FileChunk ReadTextFileChunk(std::filesystem::path const& path, int64_t const startLine, int64_t const endLine)
{
    if (startLine < 1 || endLine < 1 || startLine > endLine)
    {
        return {};
    }

    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error{ "error: could not open file" };
    }

    FileChunk result;
    int64_t lineNumber = 1;
    for (; lineNumber <= endLine; ++lineNumber)
    {
        std::string line;
        if (!std::getline(input, line))
        {
            break;
        }

        if (lineNumber >= startLine)
        {
            result.lines.push_back(std::move(line));
        }
    }

    if (result.lines.empty())
    {
        result.startLine = lineNumber;
    }
    return result;
}

void WriteTextFile(std::filesystem::path const& path, std::string_view const content)
{
    auto const parentPath = path.parent_path();
    if (!parentPath.empty() && !std::filesystem::exists(parentPath))
    {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error{ "error: could not write file" };
    }
    output << content;
}

std::filesystem::path GetExecutableDirectory()
{
    static std::filesystem::path const exe_dir =
        []() -> std::filesystem::path
        {
#if _WIN32
            char buffer[MAX_PATH];
            DWORD const size = GetModuleFileNameA(nullptr, buffer, sizeof(buffer));
            if (size > 0 && size < sizeof(buffer))
            {
                return std::filesystem::path{ buffer, buffer + size }.parent_path();
            }
#else
            try
            {
                std::filesystem::path const exe_path = std::filesystem::read_symlink("/proc/self/exe");
                if (!exe_path.empty())
                {
                    return exe_path.parent_path();
                }
            }
            catch (...) {}
#endif

            std::cerr << "error: could not get executable path. Using current path instead." << std::endl;
            return std::filesystem::current_path();
        }
        ();
    return exe_dir;
}
