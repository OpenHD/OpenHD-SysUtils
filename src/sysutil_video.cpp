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
#include "sysutil_debug.h"
#include "sysutil_platform.h"
#include "sysutil_protocol.h"
#include "sysutil_status.h"
#include "platforms_generated.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <cctype>
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

bool read_file(const std::string& path, std::string& out) {
    std::ifstream file(path);
    if (!file) return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

bool write_file_if_changed(const std::string& path, const std::string& content) {
    std::string existing;
    if (read_file(path, existing) && existing == content) {
        return true;
    }
    return write_file(path, content);
}

bool run_cmd(const std::string& cmd) {
    return std::system(cmd.c_str()) == 0;
}

std::optional<std::string> run_cmd_out(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return std::nullopt;
    }
    std::string output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
    const int status = pclose(pipe);
    if (status == -1) {
        return std::nullopt;
    }
    return output;
}

std::string trim(std::string value) {
    auto not_space = [](int ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
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

bool is_rockchip_platform() {
    const auto& info = platform_info();
    return info.platform_type == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_ZERO3W ||
           info.platform_type == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_CM3 ||
           info.platform_type == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_A ||
           info.platform_type == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_B;
}

bool has_systemctl() {
    return std::filesystem::exists("/bin/systemctl") ||
           std::filesystem::exists("/usr/bin/systemctl");
}

void apply_openhd_service_disable() {
    if (!has_systemctl()) {
        set_status("sysutils.services", "Service status",
                   "systemctl missing; cannot disable OpenHD service", 2);
        return;
    }
    run_cmd("systemctl stop openhd.service openhd_rpi.service openhd_mod.service");
    run_cmd("systemctl disable openhd.service openhd_rpi.service openhd_mod.service");
    set_status("sysutils.services", "Service status",
               "OpenHD service disabled via sysutils config", 1);
}

bool ensure_qopenhd_getty_dropin() {
    std::error_code ec;
    const std::string dropin_dir = "/etc/systemd/system/qopenhd.service.d";
    std::filesystem::create_directories(dropin_dir, ec);
    if (ec) {
        std::cerr << "Failed to create " << dropin_dir << ": " << ec.message() << std::endl;
        return false;
    }

    const std::string dropin_path = dropin_dir + "/override.conf";
    const std::string content =
        "[Service]\n"
        "ExecStartPost=-/bin/systemctl stop getty@tty1.service\n"
        "ExecStopPost=-/bin/systemctl start getty@tty1.service\n";

    if (!write_file_if_changed(dropin_path, content)) {
        std::cerr << "Failed to write " << dropin_path << std::endl;
        return false;
    }
    return true;
}

std::string unit_state(const std::string& unit) {
    if (!has_systemctl()) {
        return "no-systemctl";
    }
    auto output = run_cmd_out("systemctl is-active " + unit + " 2>/dev/null");
    if (!output.has_value()) {
        return "unknown";
    }
    return trim(*output);
}

bool start_unit(const std::string& unit) {
    if (!has_systemctl()) {
        return false;
    }
    return run_cmd("systemctl start " + unit);
}

void report_service_status(const std::string& openhd_state,
                           const std::string& qopenhd_state,
                           const std::string& getty_state,
                           const std::string& video_state,
                           bool qopenhd_requested,
                           bool rockchip_platform) {
    std::ostringstream desc;
    desc << "Services: openhd=" << openhd_state
         << ", qopenhd=" << (qopenhd_requested ? qopenhd_state : "skipped");
    if (rockchip_platform) {
        desc << ", getty@tty1=" << getty_state;
    }
    if (!video_state.empty()) {
        desc << ", openhd-video=" << video_state;
    }

    int severity = 0;
    if (openhd_state != "active") {
        severity = 2;
    }
    if (qopenhd_requested && qopenhd_state != "active") {
        severity = 2;
    }
    if (!video_state.empty() && video_state != "active") {
        severity = 2;
    }

    set_status("sysutils.services", "Service status", desc.str(), severity);
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

bool control_video_service(const std::string& action) {
    if (!has_systemctl()) {
        return false;
    }
    if (action == "start") {
        return run_cmd("systemctl start openhd-video.service");
    }
    if (action == "restart") {
        return run_cmd("systemctl restart openhd-video.service");
    }
    if (action == "stop") {
        return run_cmd("systemctl stop openhd-video.service");
    }
    return false;
}

void start_qopenhd_if_needed() {
    if (!has_systemctl()) {
        std::cerr << "systemctl not available, cannot start qopenhd." << std::endl;
        return;
    }

    if (is_rockchip_platform()) {
        if (!ensure_qopenhd_getty_dropin()) {
            std::cerr << "Failed to prepare qopenhd getty drop-in." << std::endl;
            return;
        }
    }

    run_cmd("systemctl daemon-reload");
    if (!run_cmd("systemctl start qopenhd.service")) {
        std::cerr << "Failed to start qopenhd.service" << std::endl;
    }
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
    if (is_rockchip_platform()) {
        if (has_systemctl()) {
            if (generate_decode_scripts_and_services()) {
                run_cmd("systemctl daemon-reload");
                if (!run_cmd("systemctl start openhd-video.service")) {
                    std::cerr << "Failed to start openhd-video.service" << std::endl;
                }
            } else {
                std::cerr << "Failed to generate decode scripts/services for rockchip." << std::endl;
            }
        } else {
            std::cerr << "systemctl not available, cannot start openhd-video." << std::endl;
        }

        const std::string openhd_state = unit_state("openhd.service");
        const std::string qopenhd_state = unit_state("qopenhd.service");
        const std::string getty_state = unit_state("getty@tty1.service");
        const std::string video_state = unit_state("openhd-video.service");
        report_service_status(openhd_state, qopenhd_state, getty_state,
                              video_state, true, true);
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

void start_openhd_services_if_needed() {
    const bool systemd_ok = has_systemctl();
    const bool ground = is_ground_mode();
    const bool rockchip = is_rockchip_platform();

    SysutilConfig config;
    if (load_sysutil_config(config) == ConfigLoadResult::Loaded) {
        (void)apply_openhd_debug_marker(config.debug_enabled, false);
        if (config.disable_openhd_service.value_or(false)) {
            apply_openhd_service_disable();
            return;
        }
    }

    if (!systemd_ok) {
        set_status("sysutils.services", "Service status",
                   "systemctl missing; cannot manage services", 2);
        return;
    }

    const bool openhd_started = start_unit("openhd.service");
    if (!openhd_started) {
        std::cerr << "Failed to start openhd.service" << std::endl;
    }

    if (ground) {
        start_qopenhd_if_needed();
    }

    const std::string openhd_state = unit_state("openhd.service");
    const std::string qopenhd_state = ground ? unit_state("qopenhd.service") : "skipped";
    const std::string getty_state =
        rockchip ? unit_state("getty@tty1.service") : "n/a";

    report_service_status(openhd_state, qopenhd_state, getty_state, "",
                          ground, rockchip);
}

bool is_video_request(const std::string& line) {
    auto type = extract_string_field(line, "type");
    return type.has_value() && *type == "sysutil.video.request";
}

std::string handle_video_request(const std::string& line) {
    auto action = extract_string_field(line, "action").value_or("start");
    bool ok = true;
    std::string pipeline = "ground_default";
    if (!is_ground_mode()) {
        ok = false;
    } else if (is_rpi_platform()) {
        pipeline = "rpi_process";
        if (action == "start" || action == "restart") {
            ok = start_video_process();
        } else if (action == "stop") {
            stop_video_process();
            ok = true;
        } else {
            ok = false;
        }
    } else if (is_rockchip_platform()) {
        pipeline = "systemd";
        if (action == "start" || action == "restart") {
            if (!generate_decode_scripts_and_services()) {
                ok = false;
            } else {
                run_cmd("systemctl daemon-reload");
                ok = control_video_service(action);
            }
        } else if (action == "stop") {
            ok = control_video_service(action);
        } else {
            ok = false;
        }
    } else {
        // Not implemented for non-Raspberry Pi / Rockchip platforms yet.
        std::cerr << "Ground video request ignored for platform type "
                  << platform_info().platform_type << std::endl;
        ok = false;
    }

    std::ostringstream out;
    out << "{\"type\":\"sysutil.video.response\",\"ok\":"
        << (ok ? "true" : "false")
        << ",\"action\":\"" << action
        << "\",\"pipeline\":\"" << pipeline << "\"}\n";
    return out.str();
}

} // namespace sysutil
