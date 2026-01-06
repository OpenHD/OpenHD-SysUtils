#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unordered_map>
#include <unistd.h>
#include <vector>
#include <fcntl.h>

namespace {
constexpr std::string_view kSocketDir = "/run/openhd";
constexpr std::string_view kSocketPath = "/run/openhd/openhd_sys.sock";
constexpr std::size_t kMaxLineLength = 4096;

bool setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

std::optional<std::string> extractStringField(const std::string& line, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto keyPos = line.find(needle);
    if (keyPos == std::string::npos) return std::nullopt;

    auto colonPos = line.find(':', keyPos + needle.size());
    if (colonPos == std::string::npos) return std::nullopt;

    auto quoteStart = line.find('"', colonPos + 1);
    if (quoteStart == std::string::npos) return std::nullopt;

    auto quoteEnd = line.find('"', quoteStart + 1);
    if (quoteEnd == std::string::npos || quoteEnd <= quoteStart + 1) return std::nullopt;

    return line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
}

std::optional<int> extractIntField(const std::string& line, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto keyPos = line.find(needle);
    if (keyPos == std::string::npos) return std::nullopt;

    auto colonPos = line.find(':', keyPos + needle.size());
    if (colonPos == std::string::npos) return std::nullopt;

    colonPos = line.find_first_not_of(" \t", colonPos + 1);
    if (colonPos == std::string::npos) return std::nullopt;

    char* endPtr = nullptr;
    const char* start = line.c_str() + colonPos;
    long value = std::strtol(start, &endPtr, 10);
    if (start == endPtr) return std::nullopt;
    return static_cast<int>(value);
}

void processLine(const std::string& line) {
    if (line.empty()) return;

    auto state = extractStringField(line, "state");
    auto severity = extractIntField(line, "severity");
    if (state || severity) {
        std::cout << "OpenHD state: " << (state ? *state : "UNKNOWN")
                  << " (severity=" << (severity ? std::to_string(*severity) : "?") << ")"
                  << std::endl;
    } else {
        std::cout << "OpenHD message: " << line << std::endl;
    }
}

int createAndBindSocket() {
    std::error_code ec;
    std::filesystem::create_directories(kSocketDir, ec);
    if (ec) {
        std::cerr << "Failed to create socket directory " << kSocketDir << ": " << ec.message() << std::endl;
        return -1;
    }

    ::unlink(std::string(kSocketPath).c_str());

    int serverFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0) {
        std::perror("socket");
        return -1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (kSocketPath.size() >= sizeof(addr.sun_path)) {
        std::cerr << "Socket path is too long: " << kSocketPath << std::endl;
        ::close(serverFd);
        return -1;
    }
    std::strncpy(addr.sun_path, std::string(kSocketPath).c_str(), sizeof(addr.sun_path) - 1);

    mode_t oldMask = ::umask(0);
    if (::bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        ::close(serverFd);
        ::umask(oldMask);
        return -1;
    }
    ::umask(oldMask);

    if (::chmod(std::string(kSocketPath).c_str(), 0660) < 0) {
        std::perror("chmod");
        ::close(serverFd);
        return -1;
    }

    if (::listen(serverFd, 8) < 0) {
        std::perror("listen");
        ::close(serverFd);
        return -1;
    }

    if (!setNonBlocking(serverFd)) {
        std::perror("fcntl");
    }

    return serverFd;
}

void closeClient(int fd, std::unordered_map<int, std::string>& buffers) {
    ::close(fd);
    buffers.erase(fd);
}

bool handleClientData(int fd, std::unordered_map<int, std::string>& buffers) {
    char readBuf[1024];
    while (true) {
        ssize_t count = ::read(fd, readBuf, sizeof(readBuf));
        if (count > 0) {
            auto& buffer = buffers[fd];
            buffer.append(readBuf, static_cast<std::size_t>(count));
            if (buffer.size() > kMaxLineLength * 2) {
                buffer.erase(0, buffer.size() - kMaxLineLength);
            }
            std::size_t pos = 0;
            while ((pos = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);
                if (line.size() > kMaxLineLength) {
                    line = line.substr(0, kMaxLineLength);
                }
                processLine(line);
            }
        } else if (count == 0) {
            return false;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        }
    }
    return true;
}
}  // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    if (::geteuid() != 0) {
        std::cerr << "openhd_sys_utils must be run as root." << std::endl;
        return 1;
    }

    int serverFd = createAndBindSocket();
    if (serverFd < 0) {
        return 1;
    }

    std::unordered_map<int, std::string> clientBuffers;
    std::vector<pollfd> pollFds;
    int exitCode = 0;

    while (true) {
        pollFds.clear();
        pollFds.push_back({serverFd, POLLIN, 0});
        for (const auto& entry : clientBuffers) {
            pollFds.push_back({entry.first, POLLIN | POLLERR | POLLHUP, 0});
        }

        int ready = ::poll(pollFds.data(), pollFds.size(), 500);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("poll");
            exitCode = 1;
            break;
        }

        for (const auto& pfd : pollFds) {
            if (pfd.revents == 0) continue;
            if (pfd.fd == serverFd && (pfd.revents & POLLIN)) {
                while (true) {
                    int clientFd = ::accept(serverFd, nullptr, nullptr);
                    if (clientFd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        std::perror("accept");
                        break;
                    }
                    setNonBlocking(clientFd);
                    clientBuffers.emplace(clientFd, std::string{});
                }
            } else if (pfd.fd != serverFd) {
                if (pfd.revents & (POLLERR | POLLHUP)) {
                    closeClient(pfd.fd, clientBuffers);
                } else if (pfd.revents & POLLIN) {
                    if (!handleClientData(pfd.fd, clientBuffers)) {
                        closeClient(pfd.fd, clientBuffers);
                    }
                }
            }
        }
    }

    for (auto& entry : clientBuffers) {
        ::close(entry.first);
    }
    ::close(serverFd);
    ::unlink(std::string(kSocketPath).c_str());
    return exitCode;
}
