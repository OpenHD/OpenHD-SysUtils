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

#ifndef SYSUTIL_CONFIG_H
#define SYSUTIL_CONFIG_H

#include <optional>
#include <string>

namespace sysutil {

struct SysutilConfig {
  // Cached platform type id (if known).
  std::optional<int> platform_type;
  // Cached platform name (if known).
  std::optional<std::string> platform_name;
  // Persisted debug flag.
  std::optional<bool> debug_enabled;
  // Enable hostname updates from sysutils.
  std::optional<bool> set_hostname;
  // Pending OpenHD reset request.
  std::optional<bool> reset_requested;
  // Selected camera type id.
  std::optional<int> camera_type;
  // Requested boot mode ("air" or "ground").
  std::optional<std::string> run_mode;
  // First-boot gate for one-time detection tasks.
  std::optional<bool> firstboot;
  // Detected init system (e.g. systemd or init.d).
  std::optional<std::string> init_system;
  // Detected shell type (e.g. busybox or bash).
  std::optional<std::string> shell;
  // WiFi hardware configuration.
  std::optional<bool> wifi_enable_autodetect;
  std::optional<std::string> wifi_wb_link_cards;
  std::optional<std::string> wifi_hotspot_card;
  std::optional<bool> wifi_monitor_card_emulate;
  std::optional<bool> wifi_force_no_link_but_hotspot;
  std::optional<bool> wifi_local_network_enable;
  std::optional<std::string> wifi_local_network_ssid;
  std::optional<std::string> wifi_local_network_password;
  // Networking configuration.
  std::optional<std::string> nw_ethernet_card;
  std::optional<std::string> nw_manual_forwarding_ips;
  std::optional<bool> nw_forward_to_localhost_58xx;
  // Ethernet link configuration.
  std::optional<std::string> ground_unit_ip;
  std::optional<std::string> air_unit_ip;
  std::optional<int> video_port;
  std::optional<int> telemetry_port;
  // Microhard link configuration.
  std::optional<bool> disable_microhard_detection;
  std::optional<bool> force_microhard;
  std::optional<std::string> microhard_username;
  std::optional<std::string> microhard_password;
  std::optional<std::string> microhard_ip_air;
  std::optional<std::string> microhard_ip_ground;
  std::optional<std::string> microhard_ip_range;
  std::optional<int> microhard_video_port;
  std::optional<int> microhard_telemetry_port;
  // Generic configuration.
  std::optional<bool> gen_enable_last_known_position;
  std::optional<int> gen_rf_metrics_level;
  // Service control.
  std::optional<bool> disable_openhd_service;
};

// Result of attempting to load the config file.
enum class ConfigLoadResult {
  NotFound,
  Loaded,
  Error,
};

// Returns the on-disk sysutils config path.
const char* sysutil_config_path();
// Loads config values from disk into the provided struct.
ConfigLoadResult load_sysutil_config(SysutilConfig& config);
// Writes config values only if the config file does not yet exist.
bool write_sysutil_config_if_missing(const SysutilConfig& config);
// Writes config values, overwriting any existing file.
bool write_sysutil_config(const SysutilConfig& config);
// Removes the config file if it exists.
bool remove_sysutil_config();

}  // namespace sysutil

#endif  // SYSUTIL_CONFIG_H
