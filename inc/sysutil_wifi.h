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
 * (C) OpenHD, All Rights Reserved.
 ******************************************************************************/

#ifndef SYSUTIL_WIFI_H
#define SYSUTIL_WIFI_H

#include <string>
#include <vector>

namespace sysutil {

struct WifiCardInfo {
  std::string interface_name;
  std::string driver_name;
  std::string mac;
  int phy_index = -1;
  std::string vendor_id;
  std::string device_id;
  std::string detected_type;
  std::string override_type;
  std::string effective_type;
  bool disabled = false;
  std::string tx_power;
  std::string tx_power_high;
  std::string tx_power_low;
  std::string card_name;
  std::string power_mode;
  std::string power_level;
  std::string power_lowest;
  std::string power_low;
  std::string power_mid;
  std::string power_high;
  std::string power_min;
  std::string power_max;
};

// Initializes cached Wi-Fi info (loading overrides and detecting cards).
void init_wifi_info();

// Refreshes cached Wi-Fi info (reloads overrides and re-detects cards).
void refresh_wifi_info();

// Returns true when at least one OpenHD wifibroadcast card is detected.
bool has_openhd_wifibroadcast_cards();

// Returns cached Wi-Fi card info (initializes if needed).
const std::vector<WifiCardInfo>& wifi_cards();

// Checks whether a request asks for Wi-Fi info.
bool is_wifi_request(const std::string& line);

// Builds JSON response for Wi-Fi info requests.
std::string build_wifi_response();

// Checks whether a request asks to update Wi-Fi overrides or refresh detection.
bool is_wifi_update_request(const std::string& line);

// Handles Wi-Fi update requests and returns response JSON.
std::string handle_wifi_update(const std::string& line);

// Checks whether a request asks to control RF link settings.
bool is_link_control_request(const std::string& line);

// Handles RF link control requests and returns response JSON.
std::string handle_link_control_request(const std::string& line);

}  // namespace sysutil

#endif  // SYSUTIL_WIFI_H
