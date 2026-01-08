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
 * ЖИ OpenHD, All Rights Reserved.
 ******************************************************************************/

#include "sysutil_settings.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

#include "sysutil_config.h"
#include "sysutil_hostname.h"
#include "sysutil_protocol.h"

namespace sysutil {
namespace {

constexpr const char* kResetFile = "/Config/openhd/reset.txt";
constexpr const char* kAirFile = "/Config/openhd/air.txt";
constexpr const char* kGroundFile = "/Config/openhd/ground.txt";

bool file_exists(const char* path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

void remove_file_if_exists(const char* path) {
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    (void)std::filesystem::remove(path, ec);
  }
}

std::string normalize_run_mode(std::string mode) {
  std::transform(mode.begin(), mode.end(), mode.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (mode == "air" || mode == "ground") {
    return mode;
  }
  return "";
}

std::string json_escape(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

}  // namespace

void sync_settings_from_files() {
  SysutilConfig config;
  const auto load_result = load_sysutil_config(config);
  if (load_result == ConfigLoadResult::Error) {
    return;
  }

  bool changed = false;
  if (file_exists(kResetFile)) {
    config.reset_requested = true;
    remove_file_if_exists(kResetFile);
    changed = true;
  }

  const bool has_air = file_exists(kAirFile);
  const bool has_ground = file_exists(kGroundFile);
  if (has_air || has_ground) {
    config.run_mode = has_air && !has_ground ? "air" : "ground";
    remove_file_if_exists(kAirFile);
    remove_file_if_exists(kGroundFile);
    changed = true;
  }

  if (changed) {
    (void)write_sysutil_config(config);
  }
}

bool is_settings_request(const std::string& line) {
  auto type = extract_string_field(line, "type");
  return type.has_value() && *type == "sysutil.settings.request";
}

bool is_settings_update(const std::string& line) {
  auto type = extract_string_field(line, "type");
  return type.has_value() && *type == "sysutil.settings.update";
}

std::string build_settings_response() {
  SysutilConfig config;
  const auto load_result = load_sysutil_config(config);
  if (load_result == ConfigLoadResult::Error) {
    return "{\"type\":\"sysutil.settings.response\",\"ok\":false}\n";
  }

  const bool has_reset = config.reset_requested.has_value();
  const bool reset_requested = config.reset_requested.value_or(false);
  std::string run_mode = "";
  bool has_run_mode = false;
  if (config.run_mode.has_value()) {
    run_mode = normalize_run_mode(*config.run_mode);
    has_run_mode = !run_mode.empty();
  }

  std::ostringstream out;
  out << "{\"type\":\"sysutil.settings.response\",\"ok\":true"
      << ",\"has_reset\":" << (has_reset ? "true" : "false")
      << ",\"reset_requested\":" << (reset_requested ? "true" : "false")
      << ",\"has_run_mode\":" << (has_run_mode ? "true" : "false")
      << ",\"run_mode\":\""
      << json_escape(has_run_mode ? run_mode : "unknown") << "\"}\n";
  return out.str();
}

std::string handle_settings_update(const std::string& line) {
  SysutilConfig config;
  const auto load_result = load_sysutil_config(config);
  if (load_result == ConfigLoadResult::Error) {
    return "{\"type\":\"sysutil.settings.update.response\",\"ok\":false}\n";
  }

  bool changed = false;
  bool hostname_related_change = false;
  if (auto reset_requested = extract_bool_field(line, "reset_requested");
      reset_requested.has_value()) {
    config.reset_requested = *reset_requested;
    changed = true;
  }

  if (auto run_mode_field = extract_string_field(line, "run_mode");
      run_mode_field.has_value()) {
    const auto normalized = normalize_run_mode(*run_mode_field);
    if (!normalized.empty()) {
      config.run_mode = normalized;
      changed = true;
      hostname_related_change = true;
    } else if (*run_mode_field == "unset" || *run_mode_field == "unknown") {
      config.run_mode = std::nullopt;
      changed = true;
      hostname_related_change = true;
    }
  }

  bool ok = true;
  if (changed) {
    ok = write_sysutil_config(config);
  }
  if (ok && hostname_related_change) {
    apply_hostname_if_enabled();
  }

  std::ostringstream out;
  out << "{\"type\":\"sysutil.settings.update.response\",\"ok\":"
      << (ok ? "true" : "false") << "}\n";
  return out.str();
}

}  // namespace sysutil
