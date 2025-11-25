#pragma once

#include <filesystem>
#include <string>

bool ensureDirectory(const std::filesystem::path &path);
std::string readTextFile(const std::filesystem::path &path);
bool moveFile(const std::filesystem::path &from, const std::filesystem::path &toDirectory);
