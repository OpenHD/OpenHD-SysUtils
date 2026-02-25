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

#include "sysutil_debug.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <cstdlib>

#include "sysutil_config.h"
#include "sysutil_protocol.h"

namespace sysutil {
namespace {

// Legacy debug.txt locations that enable debug mode once.
constexpr const char* kDebugFilePaths[] = {
    "/boot/openhd/debug.txt",
};
// Persistent OpenHD debug marker for log verbosity.
constexpr const char* kOpenhdDebugMarker = "/usr/local/share/openhd/debug.txt";

// Cached debug state for this process.
std::optional<bool> g_debug_enabled;

// Checks for the presence of a file.
bool file_exists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

// Removes a file if it exists.
bool remove_file(const std::string& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return true;
  }
  return std::filesystem::remove(path, ec);
}

bool has_systemctl() {
  return std::filesystem::exists("/bin/systemctl") ||
         std::filesystem::exists("/usr/bin/systemctl");
}

bool run_cmd(const std::string& cmd) {
  return std::system(cmd.c_str()) == 0;
}

bool touch_file(const std::string& path) {
  std::error_code ec;
  auto parent = std::filesystem::path(path).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
  }
  if (ec) {
    return false;
  }
  if (std::filesystem::exists(path, ec)) {
    return true;
  }
  std::ofstream file(path);
  return static_cast<bool>(file);
}

void restart_openhd_services_if_needed() {
  if (!has_systemctl()) {
    return;
  }
  (void)run_cmd("systemctl try-restart openhd.service openhd_rpi.service "
                "openhd_mod.service openhd-x20.service");
}

}  // namespace

// Initializes debug state from config and debug.txt triggers.
void init_debug_info() {
  if (g_debug_enabled.has_value()) {
    return;
  }

  SysutilConfig config;
  const auto load_result = load_sysutil_config(config);
  if (load_result == ConfigLoadResult::Loaded && config.debug_enabled) {
    g_debug_enabled = *config.debug_enabled;
  } else {
    g_debug_enabled = false;
  }

  bool debug_marker_seen = false;
  for (const auto* path : kDebugFilePaths) {
    if (!file_exists(path)) {
      continue;
    }
    debug_marker_seen = true;
    (void)remove_file(path);
  }
  if (file_exists(kOpenhdDebugMarker)) {
    debug_marker_seen = true;
  }
  if (debug_marker_seen) {
    g_debug_enabled = true;
    SysutilConfig updated_config;
    const auto load_result = load_sysutil_config(updated_config);
    if (load_result != ConfigLoadResult::Error) {
      if (load_result == ConfigLoadResult::NotFound) {
        updated_config.platform_type = std::nullopt;
        updated_config.platform_name = std::nullopt;
      }
      updated_config.debug_enabled = true;
      (void)write_sysutil_config(updated_config);
    }
  }
}

// Returns the cached debug state (initializing if needed).
bool debug_enabled() {
  if (!g_debug_enabled.has_value()) {
    init_debug_info();
  }
  return g_debug_enabled.value_or(false);
}

// Checks whether the message is a debug request.
bool is_debug_request(const std::string& line) {
  auto type = extract_string_field(line, "type");
  return type.has_value() && *type == "sysutil.debug.request";
}

// Builds a JSON response that reports debug state.
std::string build_debug_response() {
  std::ostringstream out;
  out << "{\"type\":\"sysutil.debug.response\",\"debug\":"
      << (debug_enabled() ? "true" : "false") << "}\n";
  return out.str();
}

// Checks whether the message updates debug state.
bool is_debug_update(const std::string& line) {
  auto type = extract_string_field(line, "type");
  return type.has_value() && *type == "sysutil.debug.update";
}

// Applies a debug update and returns a response payload.
std::string handle_debug_update(const std::string& line) {
  auto requested = extract_bool_field(line, "debug");
  if (!requested.has_value()) {
    requested = extract_bool_field(line, "debug_enabled");
  }
  if (!requested.has_value()) {
    return "{\"type\":\"sysutil.debug.update.response\",\"ok\":false}\n";
  }

  SysutilConfig config;
  const auto load_result = load_sysutil_config(config);
  if (load_result == ConfigLoadResult::Error) {
    return "{\"type\":\"sysutil.debug.update.response\",\"ok\":false}\n";
  }

  config.debug_enabled = *requested;
  const bool ok = write_sysutil_config(config);
  if (ok) {
    g_debug_enabled = *requested;
    (void)apply_openhd_debug_marker(requested,
                                    !config.disable_openhd_service.value_or(false));
  }

  std::ostringstream out;
  out << "{\"type\":\"sysutil.debug.update.response\",\"ok\":"
      << (ok ? "true" : "false")
      << ",\"debug\":" << (*requested ? "true" : "false") << "}\n";
  return out.str();
}

bool apply_openhd_debug_marker(const std::optional<bool>& enabled,
                               bool restart_services) {
  if (!enabled.has_value()) {
    return true;
  }
  const bool want_debug = *enabled;
  bool ok = true;
  if (want_debug) {
    ok = touch_file(kOpenhdDebugMarker);
  } else {
    ok = remove_file(kOpenhdDebugMarker);
  }
  g_debug_enabled = want_debug;
  if (restart_services) {
    restart_openhd_services_if_needed();
  }
  return ok;
}

}  // namespace sysutil
