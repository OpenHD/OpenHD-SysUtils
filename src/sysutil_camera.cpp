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

#include "sysutil_camera.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "platforms_generated.h"
#include "sysutil_config.h"
#include "sysutil_platform.h"
#include "sysutil_status.h"

namespace sysutil {
namespace {

struct CameraProfile {
  int id = -1;
  const char* rpi_link = nullptr;
  const char* rpi_ident = nullptr;
  bool rpi_cma = false;
  const char* rock_ident = nullptr;
};

const std::vector<CameraProfile> kProfiles = {
    {20, "fkms", nullptr, false, nullptr},
    {30, "kms", "ov5647", false, nullptr},
    {31, "kms", "imx219", false, nullptr},
    {32, "kms", "imx708", false, nullptr},
    {33, "kms", "imx477", false, nullptr},
    {40, "kms", "imx708", true, nullptr},
    {41, "kms", "imx519", true, nullptr},
    {42, "kms", "imx477", true, nullptr},
    {43, "kms", "imx462", true, nullptr},
    {44, "kms", "imx327", true, nullptr},
    {45, "kms", "arducam-pivariety", true, nullptr},
    {46, "kms", "arducam-pivariety", true, nullptr},
    {47, "kms", "imx662", true, nullptr},
    {60, "kms", "veyecam2m-overlay", false, nullptr},
    {61, "kms", "csimx307-overlay", false, nullptr},
    {62, "kms", "cssc132-overlay", false, nullptr},
    {63, "kms", "veye_mvcam-overlay", false, nullptr},
    {80, nullptr, nullptr, false, "rock-5b-hdmi1-8k"},
    {81, nullptr, nullptr, false, "rpi-camera-v1_3"},
    {82, nullptr, nullptr, false, "rpi-camera-v2"},
    {83, nullptr, nullptr, false, "imx708"},
    {84, nullptr, nullptr, false, "arducam-pivariety"},
    {85, nullptr, nullptr, false, "imx415"},
    {86, nullptr, nullptr, false, "arducam-pivariety"},
    {87, nullptr, nullptr, false, "arducam-pivariety"},
    {88, nullptr, nullptr, false, "ohd-jaguar"},
    {90, nullptr, nullptr, false, "hdmi-in"},
    {91, nullptr, nullptr, false, "rpi-camera-v1.3"},
    {92, nullptr, nullptr, false, "rpi-camera-v2"},
    {93, nullptr, nullptr, false, "imx708"},
    {94, nullptr, nullptr, false, "arducam-pivariety-imx462"},
    {95, nullptr, nullptr, false, "arducam-pivariety-imx519"},
    {96, nullptr, nullptr, false, "ohd-jaguar"},
};

std::optional<CameraProfile> find_profile(int id) {
  auto it =
      std::find_if(kProfiles.begin(), kProfiles.end(),
                   [id](const auto& profile) { return profile.id == id; });
  if (it == kProfiles.end()) {
    return std::nullopt;
  }
  return *it;
}

bool is_configuration_only_camera(int id) {
  // These camera types are configured by OpenHD itself and do not require a
  // device-tree change. In particular, External IP (3) must be accepted by
  // the sysutils setup API instead of being reported as an overlay failure.
  return (id >= 0 && id <= 4) || (id >= 10 && id <= 16) ||
         (id >= 70 && id <= 76) || id == 101 || id == 110 ||
         (id >= 120 && id <= 125) || id == 255;
}

bool read_file_exists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

bool copy_file_if_exists(const std::string& from, const std::string& to) {
  if (!read_file_exists(from)) {
    return false;
  }
  std::error_code ec;
  std::filesystem::copy_file(
      from, to, std::filesystem::copy_options::overwrite_existing, ec);
  return !ec;
}

std::string select_boot_config_path() {
  // Raspberry Pi OS Bookworm (required by Pi 5) mounts the active boot
  // partition at /boot/firmware. /boot/config.txt may still exist as a legacy
  // compatibility file, so always prefer the firmware path when present.
  const std::string primary = "/boot/firmware/config.txt";
  if (read_file_exists(primary)) {
    return primary;
  }
  const std::string fallback = "/boot/config.txt";
  if (read_file_exists(fallback)) {
    return fallback;
  }
  return primary;
}

bool run_command(const std::string& command) {
  int ret = std::system(command.c_str());
  if (ret != 0) {
    std::cerr << "Command failed (" << ret << "): " << command << std::endl;
    return false;
  }
  return true;
}

void apply_rpi_tuning_in(const std::string& tuning_dir, int cam_id) {
  const std::string orig = tuning_dir + "/imx477.json";
  const std::string backup = tuning_dir + "/imx477_old.json";
  if (cam_id == 42) {
    const std::string custom = tuning_dir + "/arducam-477m.json";
    if (!read_file_exists(backup)) {
      copy_file_if_exists(orig, backup);
      copy_file_if_exists(custom, orig);
    }
  } else if (cam_id == 33) {
    if (read_file_exists(backup)) {
      std::error_code ec;
      std::filesystem::remove(orig, ec);
      copy_file_if_exists(backup, orig);
      std::filesystem::remove(backup, ec);
    }
  }
}

void apply_rpi_tuning(int cam_id) {
  // Pi 5 uses the PiSP IPA pipeline; older Raspberry Pis use VC4. Check both
  // system and local prefixes so packaged and locally installed libcamera
  // layouts are supported by the same image.
  apply_rpi_tuning_in("/usr/share/libcamera/ipa/rpi/pisp", cam_id);
  apply_rpi_tuning_in("/usr/local/share/libcamera/ipa/rpi/pisp", cam_id);
  apply_rpi_tuning_in("/usr/share/libcamera/ipa/rpi/vc4", cam_id);
  apply_rpi_tuning_in("/usr/local/share/libcamera/ipa/rpi/vc4", cam_id);
}

bool update_boot_config(const std::string& dtoverlay_line,
                        const std::vector<std::string>& cam_lines) {
  const std::string path = select_boot_config_path();
  std::ifstream file(path);
  if (!file) {
    return false;
  }
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(file, line)) {
    if (line.rfind("dtoverlay=gpio-key", 0) == 0) {
      continue;
    }
    // An explicitly selected camera overlay must not compete with firmware
    // camera auto-detection, which is enabled by default on Bookworm.
    if (!cam_lines.empty() && line.rfind("camera_auto_detect=", 0) == 0) {
      continue;
    }
    lines.push_back(line);
    if (line.find("#OPENHD_DYNAMIC_CONTENT_BEGIN#") != std::string::npos) {
      break;
    }
  }
  file.close();

  lines.push_back(dtoverlay_line);
  if (!cam_lines.empty()) {
    lines.push_back("camera_auto_detect=0");
    lines.insert(lines.end(), cam_lines.begin(), cam_lines.end());
  }

  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return false;
  }
  for (const auto& item : lines) {
    out << item << "\n";
  }
  return true;
}

bool apply_rpi_config(const CameraProfile* primary_profile, int primary_cam_id,
                      const CameraProfile* secondary_profile,
                      int secondary_cam_id, bool is_modern_rpi, bool is_rpi5,
                      const std::string& primary_port,
                      const std::string& secondary_port) {
  const CameraProfile* link_profile =
      primary_profile ? primary_profile : secondary_profile;
  if (!link_profile || !link_profile->rpi_link) {
    return false;
  }
  if (primary_profile) {
    apply_rpi_tuning(primary_cam_id);
  }
  if (secondary_profile) {
    apply_rpi_tuning(secondary_cam_id);
  }
  const bool requires_cma = (primary_profile && primary_profile->rpi_cma) ||
                            (secondary_profile && secondary_profile->rpi_cma);
  const std::string cma = requires_cma ? ",cma=400M" : "";
  std::string dtoverlay_line;
  if (is_modern_rpi) {
    dtoverlay_line =
        "dtoverlay=vc4-" + std::string(link_profile->rpi_link) + "-v3d" + cma;
  } else {
    dtoverlay_line = "dtoverlay=vc4-fkms-v3d" + cma;
  }
  std::vector<std::string> cam_lines;
  if (primary_profile && primary_profile->rpi_ident) {
    auto camera_line =
        "dtoverlay=" + std::string(primary_profile->rpi_ident);
    // Raspberry Pi camera overlays default to CAM1; CAM0 needs the parameter.
    if (is_rpi5 && primary_port == "cam0") {
      camera_line += ",cam0";
    }
    cam_lines.push_back(camera_line);
  }
  if (secondary_profile && secondary_profile->rpi_ident) {
    auto camera_line =
        "dtoverlay=" + std::string(secondary_profile->rpi_ident);
    if (is_rpi5 && secondary_port == "cam0") {
      camera_line += ",cam0";
    }
    cam_lines.push_back(camera_line);
  }
  return update_boot_config(dtoverlay_line, cam_lines);
}

bool update_extlinux(const std::string& overlay_line) {
  const std::string path = "/boot/extlinux/extlinux.conf";
  std::ifstream file(path);
  if (!file) {
    return false;
  }
  std::vector<std::string> lines;
  std::string line;
  bool inserted = false;
  while (std::getline(file, line)) {
    if (line.find("fdtoverlays") != std::string::npos) {
      continue;
    }
    if (!inserted && line.find("append") != std::string::npos) {
      lines.push_back(overlay_line);
      inserted = true;
    }
    lines.push_back(line);
  }
  file.close();
  if (!inserted) {
    lines.push_back(overlay_line);
  }
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return false;
  }
  for (const auto& item : lines) {
    out << item << "\n";
  }
  return true;
}

bool apply_rock_config(const CameraProfile& profile,
                       const std::string& board_prefix) {
  if (!profile.rock_ident) {
    return false;
  }
  const std::string overlay_line =
      "        fdtoverlays  " + board_prefix + profile.rock_ident + ".dtbo";
  if (!update_extlinux(overlay_line)) {
    return false;
  }
  const std::string overlay_path =
      "/boot/dtbo/" + board_prefix + profile.rock_ident + ".dtbo";
  const std::string overlay_disabled = overlay_path + ".disabled";
  copy_file_if_exists(overlay_disabled, overlay_path);
  run_command("u-boot-update");
  return true;
}

}  // namespace

bool apply_camera_config_if_needed() {
  SysutilConfig config;
  if (load_sysutil_config(config) == ConfigLoadResult::Error) {
    return false;
  }
  const int platform = platform_info().platform_type;
  bool applied = false;
  if (platform == X_PLATFORM_TYPE_RPI_4 ||
      platform == X_PLATFORM_TYPE_RPI_CM4 ||
      platform == X_PLATFORM_TYPE_RPI_5) {
    const auto primary_profile = config.camera_type.has_value()
                                     ? find_profile(*config.camera_type)
                                     : std::nullopt;
    const auto secondary_profile =
        platform == X_PLATFORM_TYPE_RPI_5 && config.camera2_type.has_value()
            ? find_profile(*config.camera2_type)
            : std::nullopt;
    const bool is_rpi5 = platform == X_PLATFORM_TYPE_RPI_5;
    const std::string primary_port = config.camera_port.value_or("cam1");
    std::string secondary_port = config.camera2_port.value_or("cam0");
    if (is_rpi5 && primary_profile && secondary_profile &&
        secondary_port == primary_port) {
      secondary_port = primary_port == "cam0" ? "cam1" : "cam0";
    }
    applied =
        apply_rpi_config(primary_profile ? &*primary_profile : nullptr,
                         config.camera_type.value_or(-1),
                         secondary_profile ? &*secondary_profile : nullptr,
                         config.camera2_type.value_or(-1), true, is_rpi5,
                         primary_port, secondary_port);
  } else if (platform == X_PLATFORM_TYPE_RPI_OLD) {
    const auto profile = config.camera_type.has_value()
                             ? find_profile(*config.camera_type)
                             : std::nullopt;
    applied =
        apply_rpi_config(profile ? &*profile : nullptr,
                         config.camera_type.value_or(-1), nullptr, -1, false,
                         false, "cam1", "cam0");
  } else if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_ZERO3W ||
             platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_CM3) {
    if (config.camera_type.has_value()) {
      const auto profile = find_profile(*config.camera_type);
      if (profile) {
        applied = apply_rock_config(*profile, "radxa-zero3-");
      }
    }
  } else if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_A ||
             platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_B ||
             platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_CM5) {
    const auto prefix =
        platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_A ? "rock-5a-"
                                                                  : "rock-5b-";
    if (config.camera_type.has_value()) {
      const auto profile = find_profile(*config.camera_type);
      if (profile) {
        applied = apply_rock_config(*profile, prefix);
      }
    }
  }
  if (!applied && config.camera_type.has_value() &&
      is_configuration_only_camera(*config.camera_type)) {
    applied = true;
  }
  if (applied) {
    set_status("camera_setup", "Camera settings applied",
               "Camera configuration updated.");
  }
  return applied;
}

}  // namespace sysutil
