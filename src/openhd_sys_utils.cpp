#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <csignal>
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

#include "version_generated.h"
#include "sysutil_config.h"
#include "sysutil_firstboot.h"
#include "sysutil_debug.h"
#include "sysutil_hostname.h"
#include "sysutil_led.h"
#include "sysutil_part.h"
#include "sysutil_platform.h"
#include "sysutil_protocol.h"
#include "sysutil_settings.h"
#include "sysutil_status.h"
#include "sysutil_update.h"
#include "sysutil_video.h"
#include "sysutil_wifi.h"

namespace {
constexpr std::string_view kSocketDir = "/run/openhd";
constexpr std::string_view kSocketPath = "/run/openhd/openhd_sys.sock";
constexpr std::size_t kMaxLineLength = 4096;
bool gDebug = false;
volatile std::sig_atomic_t gStopRequested = 0;

void signalHandler(int) {
    gStopRequested = 1;
}

bool installSignalHandlers() {
    struct sigaction sa {};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, nullptr) < 0) {
        std::perror("sigaction SIGINT");
        return false;
    }
    if (sigaction(SIGTERM, &sa, nullptr) < 0) {
        std::perror("sigaction SIGTERM");
        return false;
    }
    return true;
}

class SocketGuard {
public:
    SocketGuard() = default;
    explicit SocketGuard(std::string_view path) : path_(path), active_(true) {}
    ~SocketGuard() {
        if (active_) {
            ::unlink(path_.c_str());
        }
    }

    SocketGuard(const SocketGuard&) = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;

    void disarm() { active_ = false; }

private:
    std::string path_;
    bool active_ = false;
};

bool setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

bool sendAll(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t written =
            ::send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd{fd, POLLOUT, 0};
            int ready = ::poll(&pfd, 1, 500);
            if (ready <= 0) {
                return false;
            }
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

bool removeStaleSocket() {
    std::error_code ec;
    if (!std::filesystem::exists(kSocketPath, ec)) {
        return true;
    }
    if (::unlink(std::string(kSocketPath).c_str()) < 0) {
        std::perror("unlink stale socket");
        return false;
    }
    std::cout << "Removed stale socket at " << kSocketPath << std::endl;
    return true;
}

void remove_space_image() {
    std::error_code ec;
    if (!std::filesystem::exists("/opt/space.img", ec)) {
        return;
    }
    if (!std::filesystem::remove("/opt/space.img", ec) || ec) {
        std::cerr << "Failed to remove /opt/space.img: " << ec.message()
                  << std::endl;
    }
}

int createAndBindSocket() {
    std::error_code ec;
    std::filesystem::create_directories(kSocketDir, ec);
    if (ec) {
        std::cerr << "Failed to create socket directory " << kSocketDir << ": " << ec.message() << std::endl;
        return -1;
    }

    if (!removeStaleSocket()) {
        return -1;
    }

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

void closeAllClients(std::unordered_map<int, std::string>& buffers) {
    for (auto& entry : buffers) {
        ::close(entry.first);
    }
    buffers.clear();
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
                if (gDebug) {
                    std::cout << "sysutils <= " << line << std::endl;
                }
                if (sysutil::is_platform_request(line)) {
                    const auto response = sysutil::build_platform_response();
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (sysutil::is_platform_update_request(line)) {
                    const auto response = sysutil::handle_platform_update(line);
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (sysutil::is_settings_request(line)) {
                    const auto response = sysutil::build_settings_response();
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (sysutil::is_settings_update(line)) {
                    const auto response = sysutil::handle_settings_update(line);
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (sysutil::is_camera_setup_request(line)) {
                    const auto response = sysutil::handle_camera_setup_request(line);
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (sysutil::is_debug_request(line)) {
                    const auto response = sysutil::build_debug_response();
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (sysutil::is_debug_update(line)) {
                    const auto response = sysutil::handle_debug_update(line);
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (sysutil::is_status_request(line)) {
                    const auto response = sysutil::build_status_response();
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (sysutil::is_wifi_request(line)) {
                    const auto response = sysutil::build_wifi_response();
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (sysutil::is_wifi_update_request(line)) {
                    const auto response = sysutil::handle_wifi_update(line);
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (sysutil::is_link_control_request(line)) {
                    const auto response = sysutil::handle_link_control_request(line);
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (sysutil::is_video_request(line)) {
                    const auto response = sysutil::handle_video_request(line);
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (sysutil::is_update_request(line)) {
                    const auto response = sysutil::handle_update_request(line);
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (auto type = sysutil::extract_string_field(line, "type");
                           type && *type == "sysutil.partitions.request") {
                    const auto response = sysutil::build_partitions_response();
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else if (auto type = sysutil::extract_string_field(line, "type");
                           type && *type == "sysutil.partition.resize.request") {
                    const auto choice =
                        sysutil::extract_string_field(line, "choice")
                            .value_or("no");
                    const auto response =
                        sysutil::handle_partition_resize_request(choice);
                    if (gDebug) {
                        std::cout << "sysutils => " << response;
                    }
                    (void)sendAll(fd, response);
                } else {
                    sysutil::handle_status_message(line);
                }
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
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-c") {
            if (!sysutil::remove_sysutil_config()) {
                std::cerr << "Failed to remove sysutils config at "
                          << sysutil::sysutil_config_path() << std::endl;
                return 1;
            }
            std::cout << "Removed sysutils config at "
                      << sysutil::sysutil_config_path() << std::endl;
            return 0;
        }
        if (arg == "-p") {
            if (!sysutil::resize_partition()) {
                std::cerr << "Partitioning task failed." << std::endl;
                return 1;
            }
            return 0;
        }
        if (arg == "-d") {
            gDebug = true;
        }
        if (arg == "-v" || arg == "--version") {
            std::cout << "OpenHD Sys Utils v" << OPENHD_SYS_UTILS_VERSION << std::endl;
            return 0;
        }
    }

    if (::geteuid() != 0) {
        std::cerr << "openhd_sys_utils must be run as root." << std::endl;
        return 1;
    }

    remove_space_image();
    sysutil::init_leds();
    sysutil::set_status("sysutils.started", "Sysutils started",
                        "Waiting for OpenHD requests.");
    sysutil::run_firstboot_tasks();
    sysutil::mount_known_partitions();
    sysutil::sync_settings_from_files();
    sysutil::init_update_worker();
    sysutil::start_openhd_services_if_needed();
    sysutil::start_ground_video_if_needed();

    sysutil::init_platform_info();
    sysutil::init_debug_info();
    sysutil::apply_hostname_if_enabled();
    sysutil::init_wifi_info();

    int serverFd = createAndBindSocket();
    if (serverFd < 0) {
        return 1;
    }

    SocketGuard socketGuard(kSocketPath);
    if (!installSignalHandlers()) {
        return 1;
    }

    std::unordered_map<int, std::string> clientBuffers;
    std::vector<pollfd> pollFds;
    int exitCode = 0;

    while (!gStopRequested) {
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
                bool keepOpen = true;
                if (pfd.revents & POLLIN) {
                    keepOpen = handleClientData(pfd.fd, clientBuffers);
                }
                if (!keepOpen || (pfd.revents & (POLLERR | POLLHUP))) {
                    closeClient(pfd.fd, clientBuffers);
                }
            }
        }
    }

    closeAllClients(clientBuffers);
    ::close(serverFd);
    socketGuard.disarm();
    ::unlink(std::string(kSocketPath).c_str());
    return exitCode;
}
