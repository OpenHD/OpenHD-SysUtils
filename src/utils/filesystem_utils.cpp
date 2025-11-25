#include "filesystem_utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>

bool ensureDirectory(const std::filesystem::path &path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return std::filesystem::is_directory(path, ec);
    }
    return std::filesystem::create_directories(path, ec);
}

std::string readTextFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool moveFile(const std::filesystem::path &from, const std::filesystem::path &toDirectory) {
    std::error_code ec;
    std::filesystem::create_directories(toDirectory, ec);
    std::filesystem::path destination = toDirectory / from.filename();
    std::filesystem::rename(from, destination, ec);
    if (ec) {
        std::cerr << "Failed to move " << from << " to " << destination << ": " << ec.message() << "\n";
        return false;
    }
    return true;
}
