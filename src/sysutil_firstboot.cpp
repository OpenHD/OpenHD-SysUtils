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

#include "sysutil_firstboot.h"

#include <filesystem>

#include "sysutil_config.h"
#include "sysutil_platform.h"

namespace sysutil {
namespace {

// Checks if a file exists without throwing.
bool file_exists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

// Detects if a symlink resolves to busybox.
bool is_symlink_to_busybox(const std::string& path) {
  std::error_code ec;
  if (!std::filesystem::is_symlink(path, ec) || ec) {
    return false;
  }
  const auto target = std::filesystem::read_symlink(path, ec);
  if (ec) {
    return false;
  }
  return target.filename() == "busybox";
}

// Determines whether systemctl or init.d should be used.
std::string detect_init_system() {
  if (file_exists("/run/systemd/system") ||
      file_exists("/bin/systemctl") ||
      file_exists("/usr/bin/systemctl")) {
    return "systemd";
  }
  if (file_exists("/etc/init.d")) {
    return "init.d";
  }
  return "unknown";
}

// Determines whether busybox or bash provides the default shell.
std::string detect_shell() {
  if (file_exists("/bin/busybox") && is_symlink_to_busybox("/bin/sh")) {
    return "busybox";
  }
  if (file_exists("/bin/bash") || file_exists("/usr/bin/bash")) {
    return "bash";
  }
  return "unknown";
}

}  // namespace

// Performs one-time detection and persists results.
void run_firstboot_tasks() {
  SysutilConfig config;
  const auto load_result = load_sysutil_config(config);
  if (load_result == ConfigLoadResult::Error) {
    return;
  }

  const bool should_run =
      (load_result == ConfigLoadResult::NotFound) ||
      !config.firstboot.has_value() || *config.firstboot;
  if (!should_run) {
    return;
  }

  const auto info = discover_platform_info();
  config.platform_type = info.platform_type;
  config.platform_name = info.platform_name;
  config.init_system = detect_init_system();
  config.shell = detect_shell();
  config.firstboot = false;
  (void)write_sysutil_config(config);
}

}  // namespace sysutil
