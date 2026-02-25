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
#include "sysutil_debug.h"
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
constexpr bool kDefaultWifiEnableAutodetect = true;
constexpr const char* kDefaultNwEthernetCard = "RPI_ETHERNET_ONLY";
constexpr int kDefaultVideoPort = 5000;
constexpr int kDefaultTelemetryPort = 5600;
constexpr const char* kDefaultMicrohardUsername = "admin";
constexpr const char* kDefaultMicrohardPassword = "qwertz1";
constexpr int kDefaultMicrohardVideoPort = 5910;
constexpr int kDefaultMicrohardTelemetryPort = 5920;
constexpr bool kRecordModeEnabled = false;

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
  if (mode == "record") {
    return kRecordModeEnabled ? mode : "air";
  }
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

      if (auto disable_openhd =
              extract_bool_field(content, "disable_openhd_service");
          disable_openhd.has_value()) {
        config.disable_openhd_service = *disable_openhd;
        changed = true;
      }
      if (auto debug = extract_bool_field(content, "debug");
          debug.has_value()) {
        config.debug_enabled = *debug;
        changed = true;
      } else if (auto debug_enabled =
                     extract_bool_field(content, "debug_enabled");
                 debug_enabled.has_value()) {
        config.debug_enabled = *debug_enabled;
        changed = true;
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
      config.run_mode = kRecordModeEnabled ? "record" : "air";
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
  const bool wifi_enable_autodetect =
      config.wifi_enable_autodetect.value_or(kDefaultWifiEnableAutodetect);
  const std::string wifi_wb_link_cards =
      config.wifi_wb_link_cards.value_or("");
  const std::string wifi_hotspot_card = config.wifi_hotspot_card.value_or("");
  const bool wifi_monitor_card_emulate =
      config.wifi_monitor_card_emulate.value_or(false);
  const bool wifi_force_no_link_but_hotspot =
      config.wifi_force_no_link_but_hotspot.value_or(false);
  const bool wifi_local_network_enable =
      config.wifi_local_network_enable.value_or(false);
  const std::string wifi_local_network_ssid =
      config.wifi_local_network_ssid.value_or("");
  const std::string wifi_local_network_password =
      config.wifi_local_network_password.value_or("");
  const std::string nw_ethernet_card =
      config.nw_ethernet_card.value_or(kDefaultNwEthernetCard);
  const std::string nw_manual_forwarding_ips =
      config.nw_manual_forwarding_ips.value_or("");
  const bool nw_forward_to_localhost_58xx =
      config.nw_forward_to_localhost_58xx.value_or(false);
  const std::string ground_unit_ip = config.ground_unit_ip.value_or("");
  const std::string air_unit_ip = config.air_unit_ip.value_or("");
  const int video_port = config.video_port.value_or(kDefaultVideoPort);
  const int telemetry_port = config.telemetry_port.value_or(kDefaultTelemetryPort);
  const bool disable_microhard_detection =
      config.disable_microhard_detection.value_or(false);
  const bool force_microhard = config.force_microhard.value_or(false);
  const std::string microhard_username =
      config.microhard_username.value_or(kDefaultMicrohardUsername);
  const std::string microhard_password =
      config.microhard_password.value_or(kDefaultMicrohardPassword);
  const std::string microhard_ip_air = config.microhard_ip_air.value_or("");
  const std::string microhard_ip_ground =
      config.microhard_ip_ground.value_or("");
  const std::string microhard_ip_range =
      config.microhard_ip_range.value_or("");
  const int microhard_video_port =
      config.microhard_video_port.value_or(kDefaultMicrohardVideoPort);
  const int microhard_telemetry_port =
      config.microhard_telemetry_port.value_or(kDefaultMicrohardTelemetryPort);
  const bool gen_enable_last_known_position =
      config.gen_enable_last_known_position.value_or(false);
  const int gen_rf_metrics_level = config.gen_rf_metrics_level.value_or(0);
  const bool disable_openhd_service =
      config.disable_openhd_service.value_or(false);

  std::ostringstream out;
  out << "{\"type\":\"sysutil.settings.response\",\"ok\":true"
      << ",\"has_reset\":" << (has_reset ? "true" : "false")
      << ",\"reset_requested\":" << (reset_requested ? "true" : "false")
      << ",\"has_camera_type\":" << (has_camera_type ? "true" : "false")
      << ",\"camera_type\":" << (has_camera_type ? *config.camera_type : 0)
      << ",\"has_run_mode\":" << (has_run_mode ? "true" : "false")
      << ",\"run_mode\":\""
      << json_escape(has_run_mode ? run_mode : "ground") << "\""
      << ",\"wifi_enable_autodetect\":"
      << (wifi_enable_autodetect ? "true" : "false")
      << ",\"wifi_wb_link_cards\":\"" << json_escape(wifi_wb_link_cards) << "\""
      << ",\"wifi_hotspot_card\":\"" << json_escape(wifi_hotspot_card) << "\""
      << ",\"wifi_monitor_card_emulate\":"
      << (wifi_monitor_card_emulate ? "true" : "false")
      << ",\"wifi_force_no_link_but_hotspot\":"
      << (wifi_force_no_link_but_hotspot ? "true" : "false")
      << ",\"wifi_local_network_enable\":"
      << (wifi_local_network_enable ? "true" : "false")
      << ",\"wifi_local_network_ssid\":\""
      << json_escape(wifi_local_network_ssid) << "\""
      << ",\"wifi_local_network_password\":\""
      << json_escape(wifi_local_network_password) << "\""
      << ",\"nw_ethernet_card\":\"" << json_escape(nw_ethernet_card) << "\""
      << ",\"nw_manual_forwarding_ips\":\""
      << json_escape(nw_manual_forwarding_ips) << "\""
      << ",\"nw_forward_to_localhost_58xx\":"
      << (nw_forward_to_localhost_58xx ? "true" : "false")
      << ",\"ground_unit_ip\":\"" << json_escape(ground_unit_ip) << "\""
      << ",\"air_unit_ip\":\"" << json_escape(air_unit_ip) << "\""
      << ",\"video_port\":" << video_port
      << ",\"telemetry_port\":" << telemetry_port
      << ",\"disable_microhard_detection\":"
      << (disable_microhard_detection ? "true" : "false")
      << ",\"force_microhard\":" << (force_microhard ? "true" : "false")
      << ",\"microhard_username\":\"" << json_escape(microhard_username) << "\""
      << ",\"microhard_password\":\"" << json_escape(microhard_password) << "\""
      << ",\"microhard_ip_air\":\"" << json_escape(microhard_ip_air) << "\""
      << ",\"microhard_ip_ground\":\"" << json_escape(microhard_ip_ground) << "\""
      << ",\"microhard_ip_range\":\"" << json_escape(microhard_ip_range) << "\""
      << ",\"microhard_video_port\":" << microhard_video_port
      << ",\"microhard_telemetry_port\":" << microhard_telemetry_port
      << ",\"gen_enable_last_known_position\":"
      << (gen_enable_last_known_position ? "true" : "false")
      << ",\"gen_rf_metrics_level\":" << gen_rf_metrics_level
      << ",\"disable_openhd_service\":"
      << (disable_openhd_service ? "true" : "false") << "}\n";
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
  bool debug_changed = false;
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

  if (auto wifi_enable_autodetect =
          extract_bool_field(line, "wifi_enable_autodetect");
      wifi_enable_autodetect.has_value()) {
    config.wifi_enable_autodetect = *wifi_enable_autodetect;
    changed = true;
  }
  if (auto wifi_wb_link_cards =
          extract_string_field(line, "wifi_wb_link_cards");
      wifi_wb_link_cards.has_value()) {
    config.wifi_wb_link_cards = *wifi_wb_link_cards;
    changed = true;
  }
  if (auto wifi_hotspot_card = extract_string_field(line, "wifi_hotspot_card");
      wifi_hotspot_card.has_value()) {
    config.wifi_hotspot_card = *wifi_hotspot_card;
    changed = true;
  }
  if (auto wifi_monitor_card_emulate =
          extract_bool_field(line, "wifi_monitor_card_emulate");
      wifi_monitor_card_emulate.has_value()) {
    config.wifi_monitor_card_emulate = *wifi_monitor_card_emulate;
    changed = true;
  }
  if (auto wifi_force_no_link_but_hotspot =
          extract_bool_field(line, "wifi_force_no_link_but_hotspot");
      wifi_force_no_link_but_hotspot.has_value()) {
    config.wifi_force_no_link_but_hotspot = *wifi_force_no_link_but_hotspot;
    changed = true;
  }
  if (auto wifi_local_network_enable =
          extract_bool_field(line, "wifi_local_network_enable");
      wifi_local_network_enable.has_value()) {
    config.wifi_local_network_enable = *wifi_local_network_enable;
    changed = true;
  }
  if (auto wifi_local_network_ssid =
          extract_string_field(line, "wifi_local_network_ssid");
      wifi_local_network_ssid.has_value()) {
    config.wifi_local_network_ssid = *wifi_local_network_ssid;
    changed = true;
  }
  if (auto wifi_local_network_password =
          extract_string_field(line, "wifi_local_network_password");
      wifi_local_network_password.has_value()) {
    config.wifi_local_network_password = *wifi_local_network_password;
    changed = true;
  }
  if (auto nw_ethernet_card = extract_string_field(line, "nw_ethernet_card");
      nw_ethernet_card.has_value()) {
    config.nw_ethernet_card = *nw_ethernet_card;
    changed = true;
  }
  if (auto nw_manual_forwarding_ips =
          extract_string_field(line, "nw_manual_forwarding_ips");
      nw_manual_forwarding_ips.has_value()) {
    config.nw_manual_forwarding_ips = *nw_manual_forwarding_ips;
    changed = true;
  }
  if (auto nw_forward_to_localhost_58xx =
          extract_bool_field(line, "nw_forward_to_localhost_58xx");
      nw_forward_to_localhost_58xx.has_value()) {
    config.nw_forward_to_localhost_58xx = *nw_forward_to_localhost_58xx;
    changed = true;
  }
  if (auto ground_unit_ip = extract_string_field(line, "ground_unit_ip");
      ground_unit_ip.has_value()) {
    config.ground_unit_ip = *ground_unit_ip;
    changed = true;
  }
  if (auto air_unit_ip = extract_string_field(line, "air_unit_ip");
      air_unit_ip.has_value()) {
    config.air_unit_ip = *air_unit_ip;
    changed = true;
  }
  if (auto video_port = extract_int_field(line, "video_port");
      video_port.has_value()) {
    config.video_port = *video_port;
    changed = true;
  }
  if (auto telemetry_port = extract_int_field(line, "telemetry_port");
      telemetry_port.has_value()) {
    config.telemetry_port = *telemetry_port;
    changed = true;
  }
  if (auto disable_microhard_detection =
          extract_bool_field(line, "disable_microhard_detection");
      disable_microhard_detection.has_value()) {
    config.disable_microhard_detection = *disable_microhard_detection;
    changed = true;
  }
  if (auto force_microhard = extract_bool_field(line, "force_microhard");
      force_microhard.has_value()) {
    config.force_microhard = *force_microhard;
    changed = true;
  }
  if (auto microhard_username = extract_string_field(line, "microhard_username");
      microhard_username.has_value()) {
    config.microhard_username = *microhard_username;
    changed = true;
  }
  if (auto microhard_password = extract_string_field(line, "microhard_password");
      microhard_password.has_value()) {
    config.microhard_password = *microhard_password;
    changed = true;
  }
  if (auto microhard_ip_air = extract_string_field(line, "microhard_ip_air");
      microhard_ip_air.has_value()) {
    config.microhard_ip_air = *microhard_ip_air;
    changed = true;
  }
  if (auto microhard_ip_ground =
          extract_string_field(line, "microhard_ip_ground");
      microhard_ip_ground.has_value()) {
    config.microhard_ip_ground = *microhard_ip_ground;
    changed = true;
  }
  if (auto microhard_ip_range =
          extract_string_field(line, "microhard_ip_range");
      microhard_ip_range.has_value()) {
    config.microhard_ip_range = *microhard_ip_range;
    changed = true;
  }
  if (auto microhard_video_port =
          extract_int_field(line, "microhard_video_port");
      microhard_video_port.has_value()) {
    config.microhard_video_port = *microhard_video_port;
    changed = true;
  }
  if (auto microhard_telemetry_port =
          extract_int_field(line, "microhard_telemetry_port");
      microhard_telemetry_port.has_value()) {
    config.microhard_telemetry_port = *microhard_telemetry_port;
    changed = true;
  }
  if (auto gen_enable_last_known_position =
          extract_bool_field(line, "gen_enable_last_known_position");
      gen_enable_last_known_position.has_value()) {
    config.gen_enable_last_known_position = *gen_enable_last_known_position;
    changed = true;
  }
  if (auto gen_rf_metrics_level =
          extract_int_field(line, "gen_rf_metrics_level");
      gen_rf_metrics_level.has_value()) {
    config.gen_rf_metrics_level = *gen_rf_metrics_level;
    changed = true;
  }
  if (auto disable_openhd_service =
          extract_bool_field(line, "disable_openhd_service");
      disable_openhd_service.has_value()) {
    config.disable_openhd_service = *disable_openhd_service;
    changed = true;
  }
  if (auto debug = extract_bool_field(line, "debug"); debug.has_value()) {
    config.debug_enabled = *debug;
    changed = true;
    debug_changed = true;
  } else if (auto debug_enabled =
                 extract_bool_field(line, "debug_enabled");
             debug_enabled.has_value()) {
    config.debug_enabled = *debug_enabled;
    changed = true;
    debug_changed = true;
  }

  bool ok = true;
  if (changed) {
    ok = write_sysutil_config(config);
  }
  if (ok && debug_changed) {
    const bool restart_openhd =
        !config.disable_openhd_service.value_or(false);
    (void)apply_openhd_debug_marker(config.debug_enabled, restart_openhd);
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
