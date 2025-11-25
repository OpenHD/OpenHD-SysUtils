#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct ProcessResult {
    bool success{false};
    int exitCode{0};
    std::string output;
};

ProcessResult runProcess(const std::vector<std::string> &args,
                         const std::string &input = {},
                         const std::optional<std::filesystem::path> &redirectStdout = std::nullopt,
                         bool mergeStderr = true);
