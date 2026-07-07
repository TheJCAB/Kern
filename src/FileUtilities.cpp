
#include "FileUtilities.h"

#include <exception>
#include <fstream>
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

void WriteTextFile(std::filesystem::path const& path, std::string_view const content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error{ "error: could not write file" };
    }
    output << content;
}

std::filesystem::path GetExecutableDirectory()
{
    try
    {
        std::filesystem::path const exe_path = std::filesystem::read_symlink("/proc/self/exe");
        if (!exe_path.empty())
        {
            return exe_path.parent_path();
        }
    }
    catch (...) {}

    return std::filesystem::current_path();
}
