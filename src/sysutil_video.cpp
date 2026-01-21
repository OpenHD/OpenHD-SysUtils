/******************************************************************************
 * OpenHD
 *
 * Licensed under the GNU General Public License (GPL) Version 3.
 *
 * This software is provided "as-is," without warranty of any kind, express or
 * implied, including but not limited to the warranties of merchantability,
 * fitness for a particular purpose, and non-infringement. For details, see the
 * full license in the LICENSE file provided with this source code.
 *
 * Non-Military Use Only:
 * This software and its associated components are explicitly intended for
 * civilian and non-military purposes. Use in any military or defense
 * applications is strictly prohibited unless explicitly and individually
 * licensed otherwise by the OpenHD Team.
 *
 * Contributors:
 * A full list of contributors can be found at the OpenHD GitHub repository:
 * https://github.com/OpenHD
 *
 * © OpenHD, All Rights Reserved.
 ******************************************************************************/

#include "sysutil_video.h"
#include "sysutil_config.h"
#include "sysutil_platform.h"
#include "sysutil_protocol.h"
#include "platforms_generated.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <sys/stat.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>

namespace sysutil {
namespace {

bool write_file(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file) return false;
    file << content;
    return true;
}

bool run_cmd(const std::string& cmd) {
    return std::system(cmd.c_str()) == 0;
}

static constexpr const char* kDefaultGroundPipeline =
    "gst-launch-1.0 udpsrc port=5600 caps='application/x-rtp, media=(string)video, "
    "clock-rate=(int)90000, encoding-name=(string)H264' ! rtph264depay ! "
    "'video/x-h264,stream-format=byte-stream' ! fdsink | fpv_video0.bin /dev/stdin";

static pid_t g_video_pid = -1;

bool is_ground_mode() {
    SysutilConfig config;
    if (load_sysutil_config(config) != ConfigLoadResult::Loaded) {
        return false;
    }
    if (!config.run_mode.has_value()) {
        return false;
    }
    return config.run_mode.value() == "ground";
}

bool is_rpi_platform() {
    const auto& info = platform_info();
    return info.platform_type == X_PLATFORM_TYPE_RPI_OLD ||
           info.platform_type == X_PLATFORM_TYPE_RPI_4 ||
           info.platform_type == X_PLATFORM_TYPE_RPI_CM4 ||
           info.platform_type == X_PLATFORM_TYPE_RPI_5;
}

void stop_video_process() {
    if (g_video_pid <= 0) {
        return;
    }
    ::kill(g_video_pid, SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        int status = 0;
        const pid_t result = ::waitpid(g_video_pid, &status, WNOHANG);
        if (result == g_video_pid) {
            g_video_pid = -1;
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ::kill(g_video_pid, SIGKILL);
    ::waitpid(g_video_pid, nullptr, 0);
    g_video_pid = -1;
}

bool start_video_process() {
    stop_video_process();
    const pid_t pid = ::fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        ::setsid();
        ::execl("/bin/sh", "sh", "-c", kDefaultGroundPipeline, static_cast<char*>(nullptr));
        _exit(127);
    }
    g_video_pid = pid;
    return true;
}

} // namespace

bool generate_decode_scripts_and_services() {
    const auto& info = platform_info();
    int type = info.platform_type;

    std::string script_content = "#!/bin/bash\n\n";
    bool supported = false;

    // RPi Logic
    if (type == X_PLATFORM_TYPE_RPI_OLD ||
        type == X_PLATFORM_TYPE_RPI_4 ||
        type == X_PLATFORM_TYPE_RPI_CM4 ||
        type == X_PLATFORM_TYPE_RPI_5) {

        script_content += "# RPi Pipeline\n";
        script_content += "gst-launch-1.0 udpsrc port=5600 caps='application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264' ! rtph264depay ! 'video/x-h264,stream-format=byte-stream' ! fdsink | fpv_video0.bin /dev/stdin\n";
        supported = true;
    }
    // Rockchip Logic
    else if (type == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_ZERO3W ||
             type == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_CM3 ||
             type == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_A ||
             type == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_B) {

        script_content += "# Rockchip Pipeline\n";
        script_content += "# Defaulting to H264/Auto. If H265 is needed, this script logic needs update or manual intervention.\n";
        script_content += "fpvue --gst-udp-port 5600 --rmode 5 --x20-auto\n";
        supported = true;
    }

    if (!supported) {
        std::cout << "Decode service generation: Unsupported platform type (" << type << ") or no specific pipeline." << std::endl;
        return false;
    }

    // Write Script
    std::string script_path = "/usr/local/bin/openhd_videodecode.sh";
    if (!write_file(script_path, script_content)) {
        std::cerr << "Failed to write decode script to " << script_path << std::endl;
        return false;
    }
    chmod(script_path.c_str(), 0755);

    // Write Service
    std::string service_path = "/etc/systemd/system/openhd-video.service";
    std::string service_content =
        "[Unit]\n"
        "Description=OpenHD Video Decode Service\n"
        "After=network.target\n\n"
        "[Service]\n"
        "ExecStart=/usr/local/bin/openhd_videodecode.sh\n"
        "Restart=always\n"
        "RestartSec=2\n\n"
        "[Install]\n"
        "WantedBy=multi-user.target\n";

    if (!write_file(service_path, service_content)) {
        std::cerr << "Failed to write decode service file to " << service_path << std::endl;
        return false;
    }

    // Enable Service
    // We try to run systemctl commands. If they fail (e.g. not running systemd), it's fine.
    run_cmd("systemctl daemon-reload");
    run_cmd("systemctl enable openhd-video.service");
    // run_cmd("systemctl start openhd-video.service");

    std::cout << "Generated and enabled openhd-video.service for platform type " << type << std::endl;
    return true;
}

void start_ground_video_if_needed() {
    if (!is_ground_mode()) {
        return;
    }
    if (!is_rpi_platform()) {
        // Not implemented for non-Raspberry Pi platforms yet.
        std::cout << "Ground video pipeline not implemented for platform type "
                  << platform_info().platform_type << std::endl;
        return;
    }
    if (!start_video_process()) {
        std::cerr << "Failed to start ground video pipeline." << std::endl;
    }
}

bool is_video_request(const std::string& line) {
    auto type = extract_string_field(line, "type");
    return type.has_value() && *type == "sysutil.video.request";
}

std::string handle_video_request(const std::string& line) {
    auto action = extract_string_field(line, "action").value_or("start");
    bool ok = true;
    if (!is_ground_mode()) {
        ok = false;
    } else if (!is_rpi_platform()) {
        // Not implemented for non-Raspberry Pi platforms yet.
        std::cerr << "Ground video request ignored for platform type "
                  << platform_info().platform_type << std::endl;
        ok = false;
    } else if (action == "start" || action == "restart") {
        ok = start_video_process();
    } else if (action == "stop") {
        stop_video_process();
        ok = true;
    } else {
        ok = false;
    }

    std::ostringstream out;
    out << "{\"type\":\"sysutil.video.response\",\"ok\":"
        << (ok ? "true" : "false")
        << ",\"action\":\"" << action
        << "\",\"pipeline\":\"ground_default\"}\n";
    return out.str();
}

} // namespace sysutil
