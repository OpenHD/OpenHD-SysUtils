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
    {48, "kms", "imx415", true, nullptr},
    {60, "kms", "veyecam2m-overlay", false, nullptr},
    {61, "kms", "csimx307-overlay", false, nullptr},
    {62, "kms", "cssc132-overlay", false, nullptr},
    {63, "kms", "veye_mvcam-overlay", false, nullptr},
    {64, "kms", "veye_gxcam", false, nullptr},
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
  auto it = std::find_if(kProfiles.begin(), kProfiles.end(),
                         [id](const auto& profile) { return profile.id == id; });
  if (it == kProfiles.end()) {
    return std::nullopt;
  }
  return *it;
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
  std::filesystem::copy_file(from, to,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
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
                        const std::string& cam_line) {
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
    if (!cam_line.empty() && line.rfind("camera_auto_detect=", 0) == 0) {
      continue;
    }
    lines.push_back(line);
    if (line.find("#OPENHD_DYNAMIC_CONTENT_BEGIN#") != std::string::npos) {
      break;
    }
  }
  file.close();

  lines.push_back(dtoverlay_line);
  if (!cam_line.empty()) {
    lines.push_back("camera_auto_detect=0");
    lines.push_back(cam_line);
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

bool apply_rpi_config(const CameraProfile& profile, int cam_id, bool is_rpi4) {
  if (!profile.rpi_link) {
    return false;
  }
  if (cam_id == 64) {
    const auto boot = std::filesystem::path(select_boot_config_path()).parent_path();
    const auto destination = boot / "overlays/veye_gxcam.dtbo";
    if (!read_file_exists(destination.string())) {
      bool restored = false;
      std::error_code error;
      std::filesystem::create_directories(destination.parent_path(), error);
      if (error) return false;
      for (const auto* source : {
               "/usr/share/openhd/overlays/veye_gxcam.dtbo",
               "/usr/local/share/openhd/kernel/overlays/veye_gxcam.dtbo"}) {
        if (copy_file_if_exists(source, destination.string())) {
          restored = true;
          break;
        }
      }
      if (!restored) {
        std::cerr << "VEYE GX boot overlay missing; install openhd-veye-gx or a complete OpenHD kernel package\n";
        return false;
      }
    }
  }
  apply_rpi_tuning(cam_id);
  const std::string cma = profile.rpi_cma ? ",cma=400M" : "";
  std::string dtoverlay_line;
  if (is_rpi4) {
    dtoverlay_line = "dtoverlay=vc4-" + std::string(profile.rpi_link) +
                     "-v3d" + cma;
  } else {
    dtoverlay_line = "dtoverlay=vc4-fkms-v3d" + cma;
  }
  std::string cam_line;
  if (profile.rpi_ident) {
    cam_line = "dtoverlay=" + std::string(profile.rpi_ident);
  }
  return update_boot_config(dtoverlay_line, cam_line);
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
  const std::string overlay_path =
      "/boot/dtbo/" + board_prefix + profile.rock_ident + ".dtbo";
  const std::string overlay_disabled = overlay_path + ".disabled";
  if (board_prefix == "radxa-zero3-" && profile.id == 93) {
    // The standard Zero 3W image stages the overlay until IMX708 is selected.
    // Radxa's generator discovers enabled overlays in /boot/dtbo.
    if (read_file_exists(overlay_disabled)
            ? !copy_file_if_exists(overlay_disabled, overlay_path)
            : !read_file_exists(overlay_path)) {
      std::cerr << "Unable to enable IMX708 overlay: " << overlay_path << '\n';
      return false;
    }
    return run_command("u-boot-update");
  }
  const std::string overlay_line =
      "        fdtoverlays  " + board_prefix + profile.rock_ident + ".dtbo";
  if (!update_extlinux(overlay_line)) {
    return false;
  }
  copy_file_if_exists(overlay_disabled, overlay_path);
  run_command("u-boot-update");
  return true;
}

bool apply_zero3w_camera(const CameraProfile& profile) {
  namespace fs = std::filesystem;
  if (!profile.rock_ident) return false;
  const std::string prefix = "/boot/dtbo/radxa-zero3-";
  const std::string selected = prefix + profile.rock_ident + ".dtbo";
  const std::string staged = selected + ".disabled";
  const bool already_enabled = read_file_exists(selected);
  if (!already_enabled && !read_file_exists(staged)) {
    std::cerr << "Missing Zero 3W camera overlay: " << staged << '\n';
    return false;
  }

  std::vector<std::string> disabled;
  bool enabled_selected = false;
  const auto restore = [&] {
    std::error_code ec;
    if (enabled_selected) fs::remove(selected, ec);
    for (const auto& path : disabled) {
      fs::rename(path + ".openhd-switch", path, ec);
      if (ec) std::cerr << "Unable to restore camera overlay: " << path << '\n';
    }
    (void)run_command("u-boot-update");
  };

  for (const auto& candidate : kProfiles) {
    if (candidate.id < 90 || candidate.id > 96 || !candidate.rock_ident) continue;
    const std::string path = prefix + candidate.rock_ident + ".dtbo";
    if (path == selected || !read_file_exists(path)) continue;
    std::error_code ec;
    // Keep an exact copy until the boot configuration has been regenerated.
    if (read_file_exists(path + ".openhd-switch")) {
      restore();
      return false;
    }
    fs::rename(path, path + ".openhd-switch", ec);
    if (ec) {
      restore();
      return false;
    }
    disabled.push_back(path);
  }
  if (!already_enabled) {
    if (!copy_file_if_exists(staged, selected)) {
      std::error_code ec;
      fs::remove(selected, ec);
      restore();
      return false;
    }
    enabled_selected = true;
  }
  if (!run_command("u-boot-update")) {
    restore();
    return false;
  }
  for (const auto& path : disabled) {
    std::error_code ec;
    if (read_file_exists(path + ".disabled")) {
      fs::remove(path + ".openhd-switch", ec);
    } else {
      fs::rename(path + ".openhd-switch", path + ".disabled", ec);
    }
    if (ec) std::cerr << "Camera overlay backup retained: " << path << ".openhd-switch\n";
  }
  return true;
}

}  // namespace

bool apply_camera_config_if_needed() {
  SysutilConfig config;
  if (load_sysutil_config(config) == ConfigLoadResult::Error) {
    return false;
  }
  if (!config.camera_type.has_value()) {
    return false;
  }
  const auto profile_opt = find_profile(config.camera_type.value());
  if (!profile_opt.has_value()) {
    return false;
  }

  const int platform = platform_info().platform_type;
  const auto& profile = profile_opt.value();
  bool applied = false;
  if (platform == X_PLATFORM_TYPE_RPI_4 ||
      platform == X_PLATFORM_TYPE_RPI_CM4 ||
      platform == X_PLATFORM_TYPE_RPI_5) {
    applied = apply_rpi_config(profile, config.camera_type.value(), true);
  } else if (platform == X_PLATFORM_TYPE_RPI_OLD) {
    applied = apply_rpi_config(profile, config.camera_type.value(), false);
  } else if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_ZERO3W) {
    applied = apply_zero3w_camera(profile);
  } else if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_CM3) {
    applied = apply_rock_config(profile, "radxa-zero3-");
  } else if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_A ||
             platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_B ||
             platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_CM5) {
    const auto prefix =
        platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_A
            ? "rock-5a-"
            : "rock-5b-";
    applied = apply_rock_config(profile, prefix);
  }
  if (applied) {
    set_status("camera_setup.complete", "Camera settings applied",
               "Camera configuration updated.");
  }
  return applied;
}

}  // namespace sysutil
