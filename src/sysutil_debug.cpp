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
#include <optional>
#include <sstream>

#include "sysutil_config.h"
#include "sysutil_protocol.h"

namespace sysutil {
namespace {

// Legacy debug.txt locations that enable debug mode once.
constexpr const char* kDebugFilePaths[] = {
    "/boot/openhd/debug.txt",
    "/usr/local/share/openhd/debug.txt",
};

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

  for (const auto* path : kDebugFilePaths) {
    if (!file_exists(path)) {
      continue;
    }
    g_debug_enabled = true;
    (void)remove_file(path);
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

}  // namespace sysutil
