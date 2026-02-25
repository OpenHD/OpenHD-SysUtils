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

#include "sysutil_config.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "sysutil_protocol.h"

namespace sysutil {
namespace {

// Sysutils config location on the target system.
constexpr const char* kConfigPath =
    "/usr/local/share/OpenHD/SysUtils/config.json";

// Escapes JSON string content for output.
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

// Returns the config path for callers that need to log or remove it.
const char* sysutil_config_path() { return kConfigPath; }

// Loads config fields from disk, if present.
ConfigLoadResult load_sysutil_config(SysutilConfig& config) {
  std::error_code ec;
  if (!std::filesystem::exists(kConfigPath, ec)) {
    return ConfigLoadResult::NotFound;
  }
  std::ifstream file(kConfigPath);
  if (!file) {
    return ConfigLoadResult::Error;
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  const std::string content = buffer.str();
  config.platform_type = extract_int_field(content, "platform_type");
  config.platform_name = extract_string_field(content, "platform_name");
  config.debug_enabled = extract_bool_field(content, "debug");
  config.set_hostname = extract_bool_field(content, "set_hostname");
  config.reset_requested = extract_bool_field(content, "reset_requested");
  config.camera_type = extract_int_field(content, "camera_type");
  config.run_mode = extract_string_field(content, "run_mode");
  config.firstboot = extract_bool_field(content, "firstboot");
  config.init_system = extract_string_field(content, "init_system");
  config.shell = extract_string_field(content, "shell");
  config.wifi_enable_autodetect =
      extract_bool_field(content, "wifi_enable_autodetect");
  config.wifi_wb_link_cards =
      extract_string_field(content, "wifi_wb_link_cards");
  config.wifi_hotspot_card = extract_string_field(content, "wifi_hotspot_card");
  config.wifi_monitor_card_emulate =
      extract_bool_field(content, "wifi_monitor_card_emulate");
  config.wifi_force_no_link_but_hotspot =
      extract_bool_field(content, "wifi_force_no_link_but_hotspot");
  config.wifi_local_network_enable =
      extract_bool_field(content, "wifi_local_network_enable");
  config.wifi_local_network_ssid =
      extract_string_field(content, "wifi_local_network_ssid");
  config.wifi_local_network_password =
      extract_string_field(content, "wifi_local_network_password");
  config.nw_ethernet_card = extract_string_field(content, "nw_ethernet_card");
  config.nw_manual_forwarding_ips =
      extract_string_field(content, "nw_manual_forwarding_ips");
  config.nw_forward_to_localhost_58xx =
      extract_bool_field(content, "nw_forward_to_localhost_58xx");
  config.ground_unit_ip = extract_string_field(content, "ground_unit_ip");
  config.air_unit_ip = extract_string_field(content, "air_unit_ip");
  config.video_port = extract_int_field(content, "video_port");
  config.telemetry_port = extract_int_field(content, "telemetry_port");
  config.disable_microhard_detection =
      extract_bool_field(content, "disable_microhard_detection");
  config.force_microhard = extract_bool_field(content, "force_microhard");
  config.microhard_username =
      extract_string_field(content, "microhard_username");
  config.microhard_password =
      extract_string_field(content, "microhard_password");
  config.microhard_ip_air = extract_string_field(content, "microhard_ip_air");
  config.microhard_ip_ground =
      extract_string_field(content, "microhard_ip_ground");
  config.microhard_ip_range =
      extract_string_field(content, "microhard_ip_range");
  config.microhard_video_port =
      extract_int_field(content, "microhard_video_port");
  config.microhard_telemetry_port =
      extract_int_field(content, "microhard_telemetry_port");
  config.gen_enable_last_known_position =
      extract_bool_field(content, "gen_enable_last_known_position");
  config.gen_rf_metrics_level =
      extract_int_field(content, "gen_rf_metrics_level");
  config.disable_openhd_service =
      extract_bool_field(content, "disable_openhd_service");
  return ConfigLoadResult::Loaded;
}

// Writes the config only when no file exists yet.
bool write_sysutil_config_if_missing(const SysutilConfig& config) {
  std::error_code ec;
  if (std::filesystem::exists(kConfigPath, ec)) {
    return true;
  }
  return write_sysutil_config(config);
}

// Writes the config file, replacing any existing file.
bool write_sysutil_config(const SysutilConfig& config) {
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(kConfigPath).parent_path(), ec);
  if (ec) {
    return false;
  }

  std::ofstream file(kConfigPath);
  if (!file) {
    return false;
  }

  file << "{\n";
  bool wrote_field = false;
  auto write_bool = [&](const char* key, const std::optional<bool>& value) {
    if (!value) {
      return;
    }
    if (wrote_field) {
      file << ",\n";
    }
    file << "  \"" << key << "\": " << (*value ? "true" : "false");
    wrote_field = true;
  };
  auto write_int = [&](const char* key, const std::optional<int>& value) {
    if (!value) {
      return;
    }
    if (wrote_field) {
      file << ",\n";
    }
    file << "  \"" << key << "\": " << *value;
    wrote_field = true;
  };
  auto write_string =
      [&](const char* key, const std::optional<std::string>& value) {
        if (!value) {
          return;
        }
        if (wrote_field) {
          file << ",\n";
        }
        file << "  \"" << key << "\": \"" << json_escape(*value) << "\"";
        wrote_field = true;
      };

  write_int("platform_type", config.platform_type);
  write_string("platform_name", config.platform_name);
  write_bool("debug", config.debug_enabled);
  write_bool("set_hostname", config.set_hostname);
  write_bool("reset_requested", config.reset_requested);
  write_int("camera_type", config.camera_type);
  write_string("run_mode", config.run_mode);
  write_bool("firstboot", config.firstboot);
  write_string("init_system", config.init_system);
  write_string("shell", config.shell);
  write_bool("wifi_enable_autodetect", config.wifi_enable_autodetect);
  write_string("wifi_wb_link_cards", config.wifi_wb_link_cards);
  write_string("wifi_hotspot_card", config.wifi_hotspot_card);
  write_bool("wifi_monitor_card_emulate", config.wifi_monitor_card_emulate);
  write_bool("wifi_force_no_link_but_hotspot",
             config.wifi_force_no_link_but_hotspot);
  write_bool("wifi_local_network_enable", config.wifi_local_network_enable);
  write_string("wifi_local_network_ssid", config.wifi_local_network_ssid);
  write_string("wifi_local_network_password", config.wifi_local_network_password);
  write_string("nw_ethernet_card", config.nw_ethernet_card);
  write_string("nw_manual_forwarding_ips", config.nw_manual_forwarding_ips);
  write_bool("nw_forward_to_localhost_58xx",
             config.nw_forward_to_localhost_58xx);
  write_string("ground_unit_ip", config.ground_unit_ip);
  write_string("air_unit_ip", config.air_unit_ip);
  write_int("video_port", config.video_port);
  write_int("telemetry_port", config.telemetry_port);
  write_bool("disable_microhard_detection", config.disable_microhard_detection);
  write_bool("force_microhard", config.force_microhard);
  write_string("microhard_username", config.microhard_username);
  write_string("microhard_password", config.microhard_password);
  write_string("microhard_ip_air", config.microhard_ip_air);
  write_string("microhard_ip_ground", config.microhard_ip_ground);
  write_string("microhard_ip_range", config.microhard_ip_range);
  write_int("microhard_video_port", config.microhard_video_port);
  write_int("microhard_telemetry_port", config.microhard_telemetry_port);
  write_bool("gen_enable_last_known_position",
             config.gen_enable_last_known_position);
  write_int("gen_rf_metrics_level", config.gen_rf_metrics_level);
  write_bool("disable_openhd_service", config.disable_openhd_service);

  file << "\n}\n";
  return static_cast<bool>(file);
}

// Removes the config file, if it exists.
bool remove_sysutil_config() {
  std::error_code ec;
  if (!std::filesystem::exists(kConfigPath, ec)) {
    return true;
  }
  return std::filesystem::remove(kConfigPath, ec);
}

}  // namespace sysutil
