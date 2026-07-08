#pragma once

#include <filesystem>
#include <string>
#include <string_view>

std::string ReadTextFile(std::filesystem::path const& path);

void WriteTextFile(std::filesystem::path const& path, std::string_view content);

std::filesystem::path GetExecutableDirectory();