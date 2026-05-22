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

#include "sysutil_serial.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "platforms_generated.h"
#include "sysutil_platform.h"

namespace sysutil {
namespace {

struct SerialRole {
  const char* name;
  const char* link_path;
};

constexpr std::array<SerialRole, 4> kSerialRoles{{
    {"Flight", "/dev/Flight"},
    {"Tracker", "/dev/Tracker"},
    {"OpenHD", "/dev/OpenHD"},
    {"Sbus", "/dev/Sbus"},
}};

void log_serial(const std::string& message) {
  std::cerr << "[sysutils][serial] " << message << std::endl;
}

bool path_exists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

bool is_existing_serial_device(const std::string& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return false;
  }
  return std::filesystem::is_character_file(path, ec) ||
         std::filesystem::is_symlink(path, ec);
}

std::string unique_key_for_path(const std::string& path) {
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(path, ec);
  if (!ec && !canonical.empty()) {
    return canonical.string();
  }
  return path;
}

void append_candidate(std::vector<std::string>& candidates,
                      std::set<std::string>& seen,
                      const std::string& path) {
  if (!is_existing_serial_device(path)) {
    return;
  }
  const auto key = unique_key_for_path(path);
  if (seen.insert(key).second) {
    candidates.push_back(path);
  }
}

bool has_prefix(const std::string& value, const char* prefix) {
  return value.rfind(prefix, 0) == 0;
}

bool is_scan_serial_name(const std::string& name) {
  return has_prefix(name, "serial") || has_prefix(name, "ttyAMA") ||
         has_prefix(name, "ttyS") || has_prefix(name, "ttyUSB") ||
         has_prefix(name, "ttyACM") || has_prefix(name, "ttymxc");
}

std::vector<std::string> known_platform_serials() {
  const int platform = platform_info().platform_type;
  if (platform == X_PLATFORM_TYPE_ORQA) {
    return {"/dev/ttymxc0", "/dev/ttymxc1"};
  }
  if (platform == X_PLATFORM_TYPE_RPI_OLD ||
      platform == X_PLATFORM_TYPE_RPI_4 ||
      platform == X_PLATFORM_TYPE_RPI_5 ||
      platform == X_PLATFORM_TYPE_RPI_CM4) {
    return {"/dev/serial0", "/dev/serial1"};
  }
  if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_A ||
      platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_B ||
      platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_CM5 ||
      platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_CM3 ||
      platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_ZERO3W) {
    return {"/dev/ttyS2", "/dev/ttyS7"};
  }
  if (platform == X_PLATFORM_TYPE_ALWINNER_X20) {
    return {"/dev/ttyS4"};
  }
  if (platform == X_PLATFORM_TYPE_LUCKFOX_RV110X ||
      platform == X_PLATFORM_TYPE_LUCKFOX_LYRA) {
    return {"/dev/ttyS3"};
  }
  return {};
}

std::vector<std::string> discover_serial_candidates() {
  std::vector<std::string> candidates;
  std::set<std::string> seen;

  for (const auto& path : known_platform_serials()) {
    append_candidate(candidates, seen, path);
  }

  std::error_code ec;
  std::vector<std::string> scanned;
  for (const auto& entry : std::filesystem::directory_iterator("/dev", ec)) {
    if (ec) {
      break;
    }
    const auto name = entry.path().filename().string();
    if (!is_scan_serial_name(name)) {
      continue;
    }
    scanned.push_back(entry.path().string());
  }
  std::sort(scanned.begin(), scanned.end());

  for (const auto& path : scanned) {
    append_candidate(candidates, seen, path);
  }
  return candidates;
}

void remove_stale_role_links() {
  for (const auto& role : kSerialRoles) {
    std::error_code ec;
    if (std::filesystem::is_symlink(role.link_path, ec) && !ec) {
      std::filesystem::remove(role.link_path, ec);
      if (ec) {
        log_serial(std::string("Failed to remove stale ") + role.link_path +
                   ": " + ec.message());
      }
    }
  }
}

std::size_t role_count_for_candidate_count(std::size_t candidate_count) {
  if (candidate_count >= kSerialRoles.size()) {
    return kSerialRoles.size();
  }
  if (candidate_count >= 2) {
    return 2;
  }
  return candidate_count;
}

void create_role_link(const SerialRole& role, const std::string& target) {
  std::error_code ec;
  if (path_exists(role.link_path) &&
      !std::filesystem::is_symlink(role.link_path, ec)) {
    log_serial(std::string("Not replacing non-symlink ") + role.link_path);
    return;
  }

  std::filesystem::create_symlink(target, role.link_path, ec);
  if (ec) {
    log_serial(std::string("Failed to link ") + role.link_path + " -> " +
               target + ": " + ec.message());
    return;
  }
  log_serial(std::string(role.link_path) + " -> " + target);
}

}  // namespace

void link_serial_ports() {
  const auto candidates = discover_serial_candidates();
  remove_stale_role_links();

  if (candidates.empty()) {
    log_serial("No serial ports found; role links not created.");
    return;
  }

  const auto role_count = role_count_for_candidate_count(candidates.size());
  for (std::size_t i = 0; i < role_count; ++i) {
    create_role_link(kSerialRoles[i], candidates[i]);
  }

  if (role_count < kSerialRoles.size()) {
    log_serial("Only " + std::to_string(candidates.size()) +
               " serial port(s) found; linked priority roles only.");
  }
}

}  // namespace sysutil
