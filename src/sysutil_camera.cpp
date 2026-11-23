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
#include <array>
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

constexpr int kZero3wImx415 = 114;

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
    {98, nullptr, nullptr, false, "imx462-pivariety"},
    {99, nullptr, nullptr, false, "tc358743-4-lane"},
    {100, nullptr, nullptr, false, "rk628-hdmi-in"},
    {kZero3wImx415, nullptr, nullptr, false, "imx415"},
};

bool is_rock3_overlay_camera(int id) {
  return (id >= 90 && id <= 100) || id == kZero3wImx415;
}

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

bool is_configuration_only_camera(int id) {
  // These camera types are configured by OpenHD itself and do not require a
  // device-tree change. In particular, External IP (3) must be accepted by
  // the sysutils setup API instead of being reported as an overlay failure.
  return (id >= 0 && id <= 4) || (id >= 10 && id <= 16) ||
         (id >= 70 && id <= 76) || id == 101 || id == 110 ||
         (id >= 120 && id <= 125) || id == 255;
}

bool apply_rock3_camera(int platform, int camera) {
  namespace fs = std::filesystem;
  const fs::path boot = "/boot/dtbo";
  const auto managed = boot / "openhd-rock3-camera.dtbo";
  const auto profile = find_profile(camera);
  const bool rock3ab = platform == X_PLATFORM_TYPE_ROCKCHIP_RK3568_RADXA_ROCK3;
  const bool rock3c = platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_ROCK3C;
  const bool cm3 = platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_CM3;
  const std::string prefix = rock3ab ? "rock-3ab-" : rock3c ? "rock-3c-" :
                             cm3 ? "radxa-cm3-" : "radxa-zero3-";
  std::string overlay;
  if (profile && is_rock3_overlay_camera(camera) && profile->rock_ident) {
    if (((rock3ab || rock3c) && camera == 99) ||
        ((rock3ab || cm3) && camera == 100) ||
        (camera == kZero3wImx415 &&
         platform != X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_ZERO3W)) {
      std::cerr << "Camera profile " << camera << " is unavailable on this board\n";
      return false;
    }
    overlay = prefix + profile->rock_ident + ".dtbo";
    if (rock3ab && camera == 91) overlay = "rock-3ab-rpi-camera-v1p3-ov5647.dtbo";
    if (rock3ab && camera == 92) overlay = "rock-3ab-rpi-camera-v2-imx219.dtbo";
    if (rock3c && camera == 91) overlay = "rock-3c-rpi-camera-v1p3.dtbo";
    if (rock3c && camera == 92) overlay = "rock-3c-rpi-camera-v2.dtbo";
    if (cm3 && camera == 91) overlay = "radxa-cm3-rpi-cm4-camera-v1p3-cam1.dtbo";
    if (cm3 && camera == 92) overlay = "radxa-cm3-rpi-cm4-camera-v2-imx219-cam1.dtbo";
    if (camera == 100 && !read_file_exists((boot / (overlay + ".disabled")).string()) &&
        !read_file_exists((boot / overlay).string())) {
      overlay = "rock-3c-radxa-hdmi-in-4k-rk628.dtbo";
    }
    if (cm3 && camera < 98 &&
        !read_file_exists((boot / (overlay + ".disabled")).string()) &&
        !read_file_exists((boot / overlay).string())) {
      overlay = "radxa-zero3-" + std::string(profile->rock_ident) + ".dtbo";
    }
  } else if (!is_configuration_only_camera(camera)) {
    return false;
  }

  const auto staging = boot / "openhd-rock3-camera.pending";
  if (!overlay.empty() &&
      !copy_file_if_exists((boot / (overlay + ".disabled")).string(), staging.string()) &&
      !copy_file_if_exists((boot / overlay).string(), staging.string())) {
    std::cerr << "Camera overlay is unavailable: " << overlay << '\n';
    return false;
  }

  std::vector<fs::path> active{managed, boot / "radxa-zero3-hdmi-in.dtbo",
                               boot / "radxa-zero3-tc358743.dtbo"};
  if (!overlay.empty()) active.push_back(boot / overlay);
  for (const auto& entry : kProfiles) {
    if (is_rock3_overlay_camera(entry.id) && entry.rock_ident) {
      // Earlier images used the Zero 3W prefix even on the CM3.
      active.push_back(boot / ("radxa-zero3-" + std::string(entry.rock_ident) + ".dtbo"));
      if (cm3 || rock3ab || rock3c)
        active.push_back(boot / (prefix + entry.rock_ident + ".dtbo"));
    }
  }
  if (rock3ab) {
    active.push_back(boot / "rock-3ab-rpi-camera-v1p3-ov5647.dtbo");
    active.push_back(boot / "rock-3ab-rpi-camera-v2-imx219.dtbo");
  }
  if (cm3) {
    active.push_back(boot / "radxa-cm3-rpi-cm4-camera-v1p3-cam1.dtbo");
    active.push_back(boot / "radxa-cm3-rpi-cm4-camera-v2-imx219-cam1.dtbo");
  }
  if (rock3c) {
    active.push_back(boot / "rock-3c-rpi-camera-v1p3.dtbo");
    active.push_back(boot / "rock-3c-rpi-camera-v2.dtbo");
  }
  if (rock3c || (!rock3ab && !cm3))
    active.push_back(boot / "rock-3c-radxa-hdmi-in-4k-rk628.dtbo");
  std::sort(active.begin(), active.end());
  active.erase(std::unique(active.begin(), active.end()), active.end());
  std::vector<fs::path> moved;
  const auto restore = [&]() {
    for (const auto& path : moved) {
      std::error_code error;
      fs::rename(path.string() + ".openhd-previous", path, error);
      if (error) std::cerr << "Cannot restore " << path << ": " << error.message() << '\n';
    }
  };
  std::error_code error;
  for (const auto& path : active) {
    if (!fs::exists(path, error) && !error) continue;
    const auto backup = path.string() + ".openhd-previous";
    if (error || fs::exists(backup, error) || error) {
      restore();
      return false;
    }
    fs::rename(path, backup, error);
    if (error) {
      restore();
      return false;
    }
    moved.push_back(path);
  }
  if (overlay.empty() && moved.empty()) return true;
  if (!overlay.empty()) fs::rename(staging, managed, error);
  if (error || !run_command("u-boot-update")) {
    if (!error) fs::remove(managed, error);
    restore();
    run_command("u-boot-update");
    return false;
  }
  for (const auto& path : moved) {
    const auto previous = path.string() + ".openhd-previous";
    const auto disabled = path.string() + ".disabled";
    if (path != managed && !read_file_exists(disabled))
      fs::rename(previous, disabled, error);
    else
      fs::remove(previous, error);
  }
  return true;
}

bool apply_ochin_cameras(int primary, int secondary) {
  static constexpr std::array<std::pair<int, const char*>, 5> sensors{{
      {102, "imx219"}, {104, "imx708"}, {106, "imx519"},
      {108, "imx462"}, {112, "imx462-pivariety"},
  }};
  namespace fs = std::filesystem;
  const fs::path boot = "/boot/dtbo";
  std::array<std::string, 2> choices;
  for (int type : {primary, secondary}) {
    if (is_configuration_only_camera(type)) continue;
    const auto sensor = std::find_if(sensors.begin(), sensors.end(),
        [type](const auto& entry) { return type == entry.first || type == entry.first + 1; });
    if (sensor == sensors.end()) return false;
    const int port = type - sensor->first;
    if (!choices[port].empty()) {
      std::cerr << "Select each OCHIN CSI connector at most once\n";
      return false;
    }
    choices[port] = "ochin-cam" + std::to_string(port) + "-" + sensor->second +
                    ".dtbo.disabled";
    if (!read_file_exists((boot / choices[port]).string())) {
      std::cerr << "OCHIN camera overlay is unavailable: " << choices[port] << '\n';
      return false;
    }
  }
  if (read_file_exists((boot / "openhd-rock3-camera.dtbo").string())) {
    std::cerr << "Disable the legacy CM3 camera overlay before selecting OCHIN cameras\n";
    return false;
  }
  const std::array<fs::path, 2> overlays{
      boot / "ochin-cam0.dtbo", boot / "ochin-cam1.dtbo"};
  std::array<bool, 2> previous{};
  std::array<bool, 2> changed{};
  for (size_t port = 0; port < overlays.size(); ++port) {
    std::error_code error;
    previous[port] = fs::symlink_status(overlays[port], error).type() != fs::file_type::not_found;
    changed[port] = choices[port].empty() ? previous[port] :
        fs::read_symlink(overlays[port], error) != fs::path(choices[port]);
  }
  if (!changed[0] && !changed[1]) return true;

  // Keep the previous selections until u-boot-update succeeds.
  std::array<bool, 2> saved{};
  std::array<bool, 2> installed{};
  bool ready = true;
  for (size_t port = 0; port < overlays.size(); ++port) {
    if (!changed[port]) continue;
    std::error_code error;
    const auto path = overlays[port].string();
    if (previous[port]) {
      fs::rename(path, path + ".openhd-previous", error);
      saved[port] = !error;
    }
    if (!error && !choices[port].empty()) {
      fs::create_symlink(choices[port], path, error);
      installed[port] = !error;
    }
    if (error) {
      ready = false;
      break;
    }
  }
  const bool applied = ready && run_command("u-boot-update");
  for (size_t port = 0; port < overlays.size(); ++port) {
    std::error_code error;
    const auto path = overlays[port].string();
    if (!applied && installed[port]) fs::remove(path, error);
    if (saved[port]) {
      if (applied) {
        fs::remove(path + ".openhd-previous", error);
      } else if (!error) {
        fs::rename(path + ".openhd-previous", path, error);
      }
    }
    if (error)
      std::cerr << "Cannot finalize OCHIN overlay " << path << ": " << error.message() << '\n';
  }
  if (!applied) run_command("u-boot-update");
  return applied;
}

}  // namespace

bool apply_camera_config_if_needed() {
  SysutilConfig config;
  if (load_sysutil_config(config) == ConfigLoadResult::Error) {
    return false;
  }
  const auto report = [](bool applied) {
    if (applied) {
      set_status("camera_setup.complete", "Camera settings applied",
                 "Camera configuration updated.");
    }
    return applied;
  };
  const int platform = platform_info().platform_type;
  if (platform == X_PLATFORM_TYPE_RADXA_CM3_OCHIN) {
    return report(apply_ochin_cameras(config.camera_type.value_or(0),
                                      config.camera2_type.value_or(255)));
  }
  if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_ZERO3W ||
      platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_CM3 ||
      platform == X_PLATFORM_TYPE_ROCKCHIP_RK3568_RADXA_ROCK3 ||
      platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_ROCK3C) {
    return report(config.camera_type && apply_rock3_camera(platform, *config.camera_type));
  }
  if (!config.camera_type.has_value()) {
    return false;
  }
  const auto profile_opt = find_profile(config.camera_type.value());
  if (!profile_opt.has_value()) {
    return false;
  }

  const auto& profile = profile_opt.value();
  bool applied = false;
  if (platform == X_PLATFORM_TYPE_RPI_4 ||
      platform == X_PLATFORM_TYPE_RPI_CM4 ||
      platform == X_PLATFORM_TYPE_RPI_5) {
    applied = apply_rpi_config(profile, config.camera_type.value(), true);
  } else if (platform == X_PLATFORM_TYPE_RPI_OLD) {
    applied = apply_rpi_config(profile, config.camera_type.value(), false);
  } else if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_A ||
             platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_B ||
             platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_CM5) {
    const auto prefix =
        platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_A
            ? "rock-5a-"
            : "rock-5b-";
    applied = apply_rock_config(profile, prefix);
  }
  return report(applied);
}

}  // namespace sysutil
