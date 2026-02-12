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

#include "sysutil_wifi.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>

#include "sysutil_protocol.h"

namespace sysutil {
namespace {

constexpr const char* kOverridesPath =
    "/usr/local/share/OpenHD/SysUtils/wifi_overrides.conf";

std::vector<WifiCardInfo> g_wifi_cards;
bool g_wifi_initialized = false;

std::string trim_copy(std::string value) {
  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
              value.end());
  return value;
}

bool file_exists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

std::optional<std::string> read_file(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    return std::nullopt;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string to_upper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return value;
}

bool equal_after_uppercase(const std::string& lhs, const std::string& rhs) {
  return to_upper(lhs) == to_upper(rhs);
}

bool contains_after_uppercase(const std::string& haystack,
                              const std::string& needle) {
  return to_upper(haystack).find(to_upper(needle)) != std::string::npos;
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

void append_cards_json(std::ostringstream& out,
                       const std::vector<WifiCardInfo>& cards) {
  out << "[";
  for (std::size_t i = 0; i < cards.size(); ++i) {
    const auto& card = cards[i];
    if (i > 0) {
      out << ",";
    }
    out << "{\"interface\":\"" << json_escape(card.interface_name) << "\""
        << ",\"driver\":\"" << json_escape(card.driver_name) << "\""
        << ",\"phy_index\":" << card.phy_index
        << ",\"mac\":\"" << json_escape(card.mac) << "\""
        << ",\"vendor_id\":\"" << json_escape(card.vendor_id) << "\""
        << ",\"device_id\":\"" << json_escape(card.device_id) << "\""
        << ",\"detected_type\":\"" << json_escape(card.detected_type) << "\""
        << ",\"override_type\":\"" << json_escape(card.override_type) << "\""
        << ",\"type\":\"" << json_escape(card.effective_type) << "\""
        << ",\"disabled\":" << (card.disabled ? "true" : "false")
        << "}";
  }
  out << "]";
}

std::unordered_map<std::string, std::string> load_overrides() {
  std::unordered_map<std::string, std::string> overrides;
  std::ifstream file(kOverridesPath);
  if (!file) {
    return overrides;
  }
  std::string line;
  while (std::getline(file, line)) {
    line = trim_copy(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    auto pos = line.find('=');
    if (pos == std::string::npos) {
      continue;
    }
    auto iface = trim_copy(line.substr(0, pos));
    auto type = trim_copy(line.substr(pos + 1));
    if (iface.empty() || type.empty()) {
      continue;
    }
    overrides[iface] = type;
  }
  return overrides;
}

bool write_overrides(const std::unordered_map<std::string, std::string>& data) {
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(kOverridesPath).parent_path(), ec);
  if (ec) {
    return false;
  }
  std::ofstream file(kOverridesPath);
  if (!file) {
    return false;
  }
  file << "# OpenHD SysUtils Wi-Fi overrides\n";
  for (const auto& entry : data) {
    file << entry.first << "=" << entry.second << "\n";
  }
  return static_cast<bool>(file);
}

std::string driver_to_type(const std::string& driver_name) {
  if (equal_after_uppercase(driver_name, "rtl88xxau_ohd")) {
    return "OPENHD_RTL_88X2AU";
  }
  if (equal_after_uppercase(driver_name, "rtl88x2au_ohd")) {
    return "OPENHD_RTL_88X2CU";
  }
  if (equal_after_uppercase(driver_name, "rtl88x2bu_ohd")) {
    return "OPENHD_RTL_88X2BU";
  }
  if (equal_after_uppercase(driver_name, "rtl88x2eu_ohd")) {
    return "OPENHD_RTL_88X2EU";
  }
  if (equal_after_uppercase(driver_name, "cnss_pci")) {
    return "QUALCOMM";
  }
  if (equal_after_uppercase(driver_name, "rtl8852bu_ohd")) {
    return "OPENHD_RTL_8852BU";
  }
  if (equal_after_uppercase(driver_name, "rtl88x2cu_ohd")) {
    return "OPENHD_RTL_88X2CU";
  }
  if (contains_after_uppercase(driver_name, "ath9k")) {
    return "ATHEROS";
  }
  if (contains_after_uppercase(driver_name, "rt2800usb")) {
    return "RALINK";
  }
  if (contains_after_uppercase(driver_name, "iwlwifi")) {
    return "INTEL";
  }
  if (contains_after_uppercase(driver_name, "brcmfmac") ||
      contains_after_uppercase(driver_name, "bcmsdh_sdmmc")) {
    return "BROADCOM";
  }
  if (contains_after_uppercase(driver_name, "aicwf_sdio")) {
    return "AIC";
  }
  if (contains_after_uppercase(driver_name, "88xxau")) {
    return "RTL_88X2AU";
  }
  if (contains_after_uppercase(driver_name, "rtw_8822bu")) {
    return "RTL_88X2BU";
  }
  if (contains_after_uppercase(driver_name, "mt7921u")) {
    return "MT_7921u";
  }
  return "UNKNOWN";
}

std::optional<std::string> extract_driver_name(const std::string& uevent) {
  const std::regex driver_regex{"DRIVER=([\\w]+)"};
  std::smatch result;
  if (!std::regex_search(uevent, result, driver_regex)) {
    return std::nullopt;
  }
  if (result.size() != 2) {
    return std::nullopt;
  }
  return result[1].str();
}

std::optional<int> read_int_file(const std::string& path) {
  auto content = read_file(path);
  if (!content) {
    return std::nullopt;
  }
  auto trimmed = trim_copy(*content);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  try {
    return std::stoi(trimmed);
  } catch (...) {
    return std::nullopt;
  }
}

std::string normalize_id(std::string value) {
  value = trim_copy(value);
  if (value.empty()) {
    return "";
  }
  if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) {
    return "0x" + to_upper(value.substr(2));
  }
  return "0x" + to_upper(value);
}

void fill_vendor_device_from_uevent(const std::string& uevent,
                                    std::string& vendor,
                                    std::string& device) {
  if (!vendor.empty() && !device.empty()) {
    return;
  }
  std::smatch match;
  std::regex pci_re{"PCI_ID=([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})"};
  if (std::regex_search(uevent, match, pci_re) && match.size() == 3) {
    if (vendor.empty()) {
      vendor = normalize_id(match[1].str());
    }
    if (device.empty()) {
      device = normalize_id(match[2].str());
    }
    return;
  }
  std::regex product_re{"PRODUCT=([0-9A-Fa-f]{4})/([0-9A-Fa-f]{4})/"}; 
  if (std::regex_search(uevent, match, product_re) && match.size() == 3) {
    if (vendor.empty()) {
      vendor = normalize_id(match[1].str());
    }
    if (device.empty()) {
      device = normalize_id(match[2].str());
    }
  }
}

WifiCardInfo build_wifi_card(const std::string& interface_name,
                             const std::unordered_map<std::string, std::string>& overrides) {
  WifiCardInfo card{};
  card.interface_name = interface_name;

  auto uevent_path = "/sys/class/net/" + interface_name + "/device/uevent";
  if (interface_name == "ath0" && !file_exists(uevent_path)) {
    uevent_path = "/sys/class/net/wifi0/device/uevent";
  }
  const auto uevent = read_file(uevent_path).value_or("");
  if (!uevent.empty()) {
    auto driver = extract_driver_name(uevent);
    if (driver) {
      card.driver_name = *driver;
    }
  }

  const auto phy_path =
      "/sys/class/net/" + interface_name + "/phy80211/index";
  const auto phy_index = read_int_file(phy_path);
  if (phy_index) {
    card.phy_index = *phy_index;
  }

  const auto mac_path = "/sys/class/net/" + interface_name + "/address";
  card.mac = trim_copy(read_file(mac_path).value_or(""));

  const auto vendor_path = "/sys/class/net/" + interface_name + "/device/vendor";
  const auto device_path = "/sys/class/net/" + interface_name + "/device/device";
  const auto usb_vendor_path = "/sys/class/net/" + interface_name + "/device/idVendor";
  const auto usb_device_path = "/sys/class/net/" + interface_name + "/device/idProduct";

  if (file_exists(vendor_path)) {
    card.vendor_id = normalize_id(read_file(vendor_path).value_or(""));
  }
  if (file_exists(device_path)) {
    card.device_id = normalize_id(read_file(device_path).value_or(""));
  }
  if (card.vendor_id.empty() && file_exists(usb_vendor_path)) {
    card.vendor_id = normalize_id(read_file(usb_vendor_path).value_or(""));
  }
  if (card.device_id.empty() && file_exists(usb_device_path)) {
    card.device_id = normalize_id(read_file(usb_device_path).value_or(""));
  }
  if (!uevent.empty()) {
    fill_vendor_device_from_uevent(uevent, card.vendor_id, card.device_id);
  }

  card.detected_type = driver_to_type(card.driver_name);

  auto override_it = overrides.find(interface_name);
  if (override_it != overrides.end()) {
    card.override_type = override_it->second;
    if (equal_after_uppercase(card.override_type, "DISABLED")) {
      card.disabled = true;
      card.effective_type = card.detected_type;
    } else {
      card.effective_type = card.override_type;
    }
  } else {
    card.effective_type = card.detected_type;
  }

  return card;
}

std::vector<WifiCardInfo> detect_wifi_cards(
    const std::unordered_map<std::string, std::string>& overrides) {
  std::vector<WifiCardInfo> cards;
  std::error_code ec;
  std::filesystem::directory_iterator dir("/sys/class/net", ec);
  if (ec) {
    return cards;
  }
  for (const auto& entry : dir) {
    const auto iface = entry.path().filename().string();
    std::error_code exists_ec;
    if (!std::filesystem::exists(entry.path() / "phy80211", exists_ec)) {
      continue;
    }
    cards.push_back(build_wifi_card(iface, overrides));
  }
  return cards;
}

void refresh_wifi_info() {
  const auto overrides = load_overrides();
  g_wifi_cards = detect_wifi_cards(overrides);
  g_wifi_initialized = true;
}

}  // namespace

void init_wifi_info() {
  refresh_wifi_info();
}

const std::vector<WifiCardInfo>& wifi_cards() {
  if (!g_wifi_initialized) {
    refresh_wifi_info();
  }
  return g_wifi_cards;
}

bool is_wifi_request(const std::string& line) {
  auto type = extract_string_field(line, "type");
  return type.has_value() && *type == "sysutil.wifi.request";
}

std::string build_wifi_response() {
  const auto& cards = wifi_cards();
  std::ostringstream out;
  out << "{\"type\":\"sysutil.wifi.response\",\"ok\":true,\"cards\":";
  append_cards_json(out, cards);
  out << "}\n";
  return out.str();
}

bool is_wifi_update_request(const std::string& line) {
  auto type = extract_string_field(line, "type");
  return type.has_value() && *type == "sysutil.wifi.update";
}

std::string handle_wifi_update(const std::string& line) {
  auto action = extract_string_field(line, "action").value_or("refresh");
  const auto iface = extract_string_field(line, "interface");
  const auto override_type = extract_string_field(line, "override_type");

  bool ok = true;
  auto overrides = load_overrides();

  if (action == "set") {
    if (!iface || iface->empty()) {
      ok = false;
    } else if (!override_type || override_type->empty() ||
               equal_after_uppercase(*override_type, "AUTO")) {
      overrides.erase(*iface);
      ok = write_overrides(overrides);
    } else {
      overrides[*iface] = *override_type;
      ok = write_overrides(overrides);
    }
  } else if (action == "clear") {
    if (iface && !iface->empty()) {
      overrides.erase(*iface);
      ok = write_overrides(overrides);
    } else {
      overrides.clear();
      ok = write_overrides(overrides);
    }
  } else if (action == "refresh" || action == "detect") {
    ok = true;
  } else {
    ok = false;
  }

  if (ok) {
    refresh_wifi_info();
  }

  std::ostringstream out;
  out << "{\"type\":\"sysutil.wifi.update.response\",\"ok\":"
      << (ok ? "true" : "false")
      << ",\"action\":\"" << json_escape(action) << "\"";
  if (ok) {
    out << ",\"cards\":";
    append_cards_json(out, wifi_cards());
  }
  out << "}\n";
  return out.str();
}

}  // namespace sysutil
