#include "process.h"

#include <array>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

ProcessResult runProcess(const std::vector<std::string> &args,
                         const std::string &input,
                         const std::optional<std::filesystem::path> &redirectStdout,
                         bool mergeStderr) {
    ProcessResult result;
    if (args.empty()) {
        return result;
    }

    int inputPipe[2]{-1, -1};
    if (!input.empty() && pipe(inputPipe) == -1) {
        perror("pipe");
        return result;
    }

    int outputPipe[2]{-1, -1};
    bool captureOutput = !redirectStdout.has_value();
    if (captureOutput && pipe(outputPipe) == -1) {
        perror("pipe");
        if (inputPipe[0] != -1) {
            close(inputPipe[0]);
            close(inputPipe[1]);
        }
        return result;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        if (inputPipe[0] != -1) {
            close(inputPipe[0]);
            close(inputPipe[1]);
        }
        if (outputPipe[0] != -1) {
            close(outputPipe[0]);
            close(outputPipe[1]);
        }
        return result;
    }

    if (pid == 0) {
        // Child
        if (!input.empty()) {
            close(inputPipe[1]);
            dup2(inputPipe[0], STDIN_FILENO);
            close(inputPipe[0]);
        }

        if (redirectStdout.has_value()) {
            int fd = ::open(redirectStdout->c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
            if (fd >= 0) {
                dup2(fd, STDOUT_FILENO);
                if (mergeStderr) {
                    dup2(fd, STDERR_FILENO);
                }
                close(fd);
            }
        } else {
            close(outputPipe[0]);
            dup2(outputPipe[1], STDOUT_FILENO);
            if (mergeStderr) {
                dup2(outputPipe[1], STDERR_FILENO);
            }
            close(outputPipe[1]);
        }

        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const auto &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        std::perror("execvp");
        _exit(127);
    }

    // Parent
    if (!input.empty()) {
        close(inputPipe[0]);
        ssize_t written = write(inputPipe[1], input.data(), input.size());
        (void)written;
        close(inputPipe[1]);
    }

    if (captureOutput) {
        close(outputPipe[1]);
        std::array<char, 4096> buffer{};
        ssize_t count;
        while ((count = read(outputPipe[0], buffer.data(), buffer.size())) > 0) {
            result.output.append(buffer.data(), static_cast<size_t>(count));
        }
        close(outputPipe[0]);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
        result.success = result.exitCode == 0;
    }
    return result;
}
