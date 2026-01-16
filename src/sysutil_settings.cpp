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

#include "sysutil_settings.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "sysutil_camera.h"
#include "sysutil_config.h"
#include "sysutil_hostname.h"
#include "sysutil_protocol.h"
#include "sysutil_status.h"

namespace sysutil {
namespace {

constexpr const char* kResetFile = "/Config/openhd/reset.txt";
constexpr const char* kAirFile = "/Config/openhd/air.txt";
constexpr const char* kGroundFile = "/Config/openhd/ground.txt";
constexpr const char* kRecordFile = "/Config/openhd/record.txt";
constexpr const char* kSettingsJson = "/Config/settings.json";
constexpr const char* kSettingsJsonSub = "/Config/openhd/settings.json";

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
  if (mode == "air" || mode == "ground" || mode == "record") {
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

std::optional<int> read_int_file(const char* path) {
  std::ifstream file(path);
  if (!file) {
    return std::nullopt;
  }
  int value = 0;
  file >> value;
  if (!file) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

void sync_settings_from_files() {
  SysutilConfig config;
  const auto load_result = load_sysutil_config(config);
  if (load_result == ConfigLoadResult::Error) {
    return;
  }

  bool changed = false;

  // Check for settings.json (new format)
  std::string json_path;
  if (file_exists(kSettingsJson)) {
    json_path = kSettingsJson;
  } else if (file_exists(kSettingsJsonSub)) {
    json_path = kSettingsJsonSub;
  }

  if (!json_path.empty()) {
    std::ifstream file(json_path);
    if (file) {
      std::ostringstream buffer;
      buffer << file.rdbuf();
      const std::string content = buffer.str();

      // Parse camera (supports int or string)
      auto cam_int = extract_int_field(content, "camera");
      if (cam_int) {
        config.camera_type = *cam_int;
        changed = true;
      } else {
        auto cam_str = extract_string_field(content, "camera");
        if (cam_str) {
          try {
            config.camera_type = std::stoi(*cam_str);
            changed = true;
          } catch (...) {
            // Invalid integer string
          }
        }
      }

      // Parse role
      auto role = extract_string_field(content, "role");
      if (role) {
        const auto mode = normalize_run_mode(*role);
        if (!mode.empty()) {
          config.run_mode = mode;
          changed = true;
        }
      }

      // sbc field is currently ignored as platform detection handles it.
    }
    remove_file_if_exists(json_path.c_str());
  }

  if (file_exists(kResetFile)) {
    config.reset_requested = true;
    remove_file_if_exists(kResetFile);
    changed = true;
  }

  const bool has_record = file_exists(kRecordFile);
  const bool has_air = file_exists(kAirFile);
  const bool has_ground = file_exists(kGroundFile);
  if (has_record || has_air || has_ground) {
    if (has_record) {
      config.run_mode = "record";
    } else {
      config.run_mode = has_air && !has_ground ? "air" : "ground";
    }
    remove_file_if_exists(kRecordFile);
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

bool is_camera_setup_request(const std::string& line) {
  auto type = extract_string_field(line, "type");
  return type.has_value() && *type == "sysutil.camera.setup.request";
}

std::string build_settings_response() {
  SysutilConfig config;
  const auto load_result = load_sysutil_config(config);
  if (load_result == ConfigLoadResult::Error) {
    return "{\"type\":\"sysutil.settings.response\",\"ok\":false}\n";
  }

  const bool has_reset = config.reset_requested.has_value();
  const bool reset_requested = config.reset_requested.value_or(false);
  std::string run_mode = "ground";
  bool has_run_mode = true;
  if (config.run_mode.has_value()) {
    const auto configured_mode = normalize_run_mode(*config.run_mode);
    if (!configured_mode.empty()) {
      run_mode = configured_mode;
    }
  }
  const bool has_camera_type = config.camera_type.has_value();

  std::ostringstream out;
  out << "{\"type\":\"sysutil.settings.response\",\"ok\":true"
      << ",\"has_reset\":" << (has_reset ? "true" : "false")
      << ",\"reset_requested\":" << (reset_requested ? "true" : "false")
      << ",\"has_camera_type\":" << (has_camera_type ? "true" : "false")
      << ",\"camera_type\":" << (has_camera_type ? *config.camera_type : 0)
      << ",\"has_run_mode\":" << (has_run_mode ? "true" : "false")
      << ",\"run_mode\":\""
      << json_escape(has_run_mode ? run_mode : "ground") << "\"}\n";
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

  if (auto camera_type = extract_int_field(line, "camera_type");
      camera_type.has_value()) {
    config.camera_type = *camera_type;
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

std::string handle_camera_setup_request(const std::string& line) {
  SysutilConfig config;
  const auto load_result = load_sysutil_config(config);
  if (load_result == ConfigLoadResult::Error) {
    return "{\"type\":\"sysutil.camera.setup.response\",\"ok\":false}\n";
  }

  auto camera_type = extract_int_field(line, "camera_type");
  if (!camera_type.has_value()) {
    return "{\"type\":\"sysutil.camera.setup.response\",\"ok\":false,\"message\":\"missing camera_type\"}\n";
  }

  config.camera_type = *camera_type;
  if (!write_sysutil_config(config)) {
    return "{\"type\":\"sysutil.camera.setup.response\",\"ok\":false,\"message\":\"config write failed\"}\n";
  }

  set_status("camera_setup", "Camera setup requested",
             "Applying camera configuration.");
  std::thread([]() {
    if (!apply_camera_config_if_needed()) {
      set_status("camera_setup", "Camera setup failed",
                 "Unable to apply camera configuration.", 2);
      return;
    }
    set_status("reboot", "Reboot initiated",
               "Rebooting after camera setup.");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::system("reboot");
  }).detach();

  return "{\"type\":\"sysutil.camera.setup.response\",\"ok\":true,\"applied\":false,\"message\":\"queued\"}\n";
}

}  // namespace sysutil
