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
#include "sysutil_platform.h"
#include "platforms_generated.h"
#include <fstream>
#include <iostream>
#include <string>
#include <cstdlib>
#include <sys/stat.h>

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

} // namespace sysutil
