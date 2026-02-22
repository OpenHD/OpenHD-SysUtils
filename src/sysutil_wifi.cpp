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
#include <cstring>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <poll.h>
#include <regex>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unordered_map>
#include <unistd.h>

#include "sysutil_protocol.h"

namespace sysutil {
namespace {

constexpr const char* kOverridesPath =
    "/usr/local/share/OpenHD/SysUtils/wifi_overrides.conf";
constexpr const char* kTxPowerOverridesPath =
    "/usr/local/share/OpenHD/SysUtils/wifi_txpower.conf";
constexpr const char* kWifiCardsPath =
    "/usr/local/share/OpenHD/SysUtils/wifi_cards.json";
constexpr const char* kOpenHdControlSocketPath =
    "/run/openhd/openhd_ctrl.sock";
constexpr std::size_t kMaxControlLineLength = 4096;
constexpr auto kOpenHdControlTimeout = std::chrono::milliseconds(900);

std::vector<WifiCardInfo> g_wifi_cards;
bool g_wifi_initialized = false;

struct WifiTxPowerOverride {
  std::string tx_power;
  std::string tx_power_high;
  std::string tx_power_low;
  std::string card_name;
  std::string power_level;
  std::string profile_vendor_id;
  std::string profile_device_id;
  std::string profile_chipset;
};

struct WifiCardProfile {
  std::string vendor_id;
  std::string device_id;
  std::string chipset;
  std::string name;
  std::string power_mode;
  int min_mw = 0;
  int max_mw = 0;
  int lowest_mw = 0;
  int low_mw = 0;
  int mid_mw = 0;
  int high_mw = 0;
};

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

bool write_all(int fd, const std::string& data) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t written =
        ::send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
    if (written > 0) {
      offset += static_cast<std::size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    return false;
  }
  return true;
}

std::optional<std::string> read_line_with_timeout(
    int fd, std::chrono::milliseconds timeout) {
  std::string buffer;
  buffer.reserve(256);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  char tmp[256];

  while (buffer.size() < kMaxControlLineLength) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return std::nullopt;
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    const int ready = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::nullopt;
    }
    if (ready == 0) {
      return std::nullopt;
    }
    const ssize_t count = ::recv(fd, tmp, sizeof(tmp), 0);
    if (count > 0) {
      buffer.append(tmp, static_cast<std::size_t>(count));
      const auto pos = buffer.find('\n');
      if (pos != std::string::npos) {
        return buffer.substr(0, pos);
      }
      continue;
    }
    if (count == 0) {
      return std::nullopt;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      continue;
    }
    return std::nullopt;
  }

  return std::nullopt;
}

std::optional<std::string> send_openhd_control(const std::string& payload) {
  if (!file_exists(kOpenHdControlSocketPath)) {
    return std::nullopt;
  }

  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return std::nullopt;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, kOpenHdControlSocketPath,
               sizeof(addr.sun_path) - 1);

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return std::nullopt;
  }

  const bool sent_ok = write_all(fd, payload);
  if (!sent_ok) {
    ::close(fd);
    return std::nullopt;
  }

  auto response = read_line_with_timeout(fd, kOpenHdControlTimeout);
  ::close(fd);
  return response;
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
        << ",\"tx_power\":\"" << json_escape(card.tx_power) << "\""
        << ",\"tx_power_high\":\"" << json_escape(card.tx_power_high) << "\""
        << ",\"tx_power_low\":\"" << json_escape(card.tx_power_low) << "\""
        << ",\"card_name\":\"" << json_escape(card.card_name) << "\""
        << ",\"power_mode\":\"" << json_escape(card.power_mode) << "\""
        << ",\"power_level\":\"" << json_escape(card.power_level) << "\""
        << ",\"power_lowest\":\"" << json_escape(card.power_lowest) << "\""
        << ",\"power_low\":\"" << json_escape(card.power_low) << "\""
        << ",\"power_mid\":\"" << json_escape(card.power_mid) << "\""
        << ",\"power_high\":\"" << json_escape(card.power_high) << "\""
        << ",\"power_min\":\"" << json_escape(card.power_min) << "\""
        << ",\"power_max\":\"" << json_escape(card.power_max) << "\""
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

bool has_tx_power_values(const WifiTxPowerOverride& entry) {
  return !entry.tx_power.empty() || !entry.tx_power_high.empty() ||
         !entry.tx_power_low.empty() || !entry.card_name.empty() ||
         !entry.power_level.empty() || !entry.profile_vendor_id.empty() ||
         !entry.profile_device_id.empty() || !entry.profile_chipset.empty();
}

std::vector<std::string> extract_array_objects(const std::string& content,
                                               const std::string& key) {
  std::vector<std::string> objects;
  const std::string needle = "\"" + key + "\"";
  auto key_pos = content.find(needle);
  if (key_pos == std::string::npos) {
    return objects;
  }
  auto colon_pos = content.find(':', key_pos + needle.size());
  if (colon_pos == std::string::npos) {
    return objects;
  }
  auto array_pos = content.find('[', colon_pos + 1);
  if (array_pos == std::string::npos) {
    return objects;
  }

  bool in_string = false;
  bool escape = false;
  int depth = 0;
  std::size_t obj_start = std::string::npos;
  for (std::size_t pos = array_pos + 1; pos < content.size(); ++pos) {
    char ch = content[pos];
    if (in_string) {
      if (escape) {
        escape = false;
      } else if (ch == '\\') {
        escape = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }
    if (ch == '"') {
      in_string = true;
      continue;
    }
    if (ch == '{') {
      if (depth == 0) {
        obj_start = pos;
      }
      ++depth;
      continue;
    }
    if (ch == '}') {
      if (depth > 0) {
        --depth;
        if (depth == 0 && obj_start != std::string::npos) {
          objects.emplace_back(content.substr(obj_start, pos - obj_start + 1));
          obj_start = std::string::npos;
        }
      }
      continue;
    }
    if (ch == ']' && depth == 0) {
      break;
    }
  }
  return objects;
}

std::string to_string_if(int value) {
  if (value <= 0) {
    return "";
  }
  return std::to_string(value);
}

std::string normalize_id(std::string value);
std::vector<WifiCardProfile> default_wifi_card_profiles();

std::string normalize_chipset(std::string value) {
  return to_upper(trim_copy(value));
}

std::vector<WifiCardProfile> load_wifi_card_profiles() {
  std::vector<WifiCardProfile> profiles;
  auto content = read_file(kWifiCardsPath);
  if (!content) {
    return default_wifi_card_profiles();
  }
  auto objects = extract_array_objects(*content, "cards");
  if (objects.empty()) {
    return default_wifi_card_profiles();
  }

  for (const auto& object : objects) {
    auto vendor = extract_string_field(object, "vendor_id");
    auto device = extract_string_field(object, "device_id");
    if (!vendor || !device) {
      continue;
    }
    WifiCardProfile profile{};
    profile.vendor_id = normalize_id(*vendor);
    profile.device_id = normalize_id(*device);
    profile.chipset =
        normalize_chipset(extract_string_field(object, "chipset").value_or(""));
    profile.name = extract_string_field(object, "name").value_or("");
    profile.power_mode = to_upper(extract_string_field(object, "power_mode").value_or("mw"));
    if (profile.power_mode == "FIXED") {
      profile.min_mw = 0;
      profile.max_mw = 0;
      profile.lowest_mw = 0;
      profile.low_mw = 0;
      profile.mid_mw = 0;
      profile.high_mw = 0;
      profiles.push_back(profile);
      continue;
    }
    profile.min_mw = extract_int_field(object, "min_mw").value_or(0);
    profile.max_mw = extract_int_field(object, "max_mw").value_or(0);
    profile.lowest_mw = extract_int_field(object, "lowest").value_or(0);
    profile.low_mw = extract_int_field(object, "low").value_or(0);
    profile.mid_mw = extract_int_field(object, "mid").value_or(0);
    profile.high_mw = extract_int_field(object, "high").value_or(0);

    if (profile.min_mw <= 0 && profile.lowest_mw > 0) {
      profile.min_mw = profile.lowest_mw;
    }
    if (profile.max_mw <= 0 && profile.high_mw > 0) {
      profile.max_mw = profile.high_mw;
    }
    if (profile.lowest_mw <= 0 && profile.min_mw > 0) {
      profile.lowest_mw = profile.min_mw;
    }
    if (profile.low_mw <= 0 && profile.lowest_mw > 0) {
      profile.low_mw = profile.lowest_mw;
    }
    if (profile.mid_mw <= 0 && profile.low_mw > 0) {
      profile.mid_mw = profile.low_mw;
    }
    if (profile.high_mw <= 0 && profile.max_mw > 0) {
      profile.high_mw = profile.max_mw;
    }

    profiles.push_back(profile);
  }
  if (profiles.empty()) {
    return default_wifi_card_profiles();
  }
  return profiles;
}

const WifiCardProfile* find_wifi_profile(
    const std::vector<WifiCardProfile>& profiles,
    const std::string& vendor_id,
    const std::string& device_id,
    const std::string& chipset) {
  const WifiCardProfile* vendor_device_match = nullptr;
  const WifiCardProfile* generic_match = nullptr;
  for (const auto& profile : profiles) {
    if (equal_after_uppercase(profile.vendor_id, vendor_id) &&
        equal_after_uppercase(profile.device_id, device_id)) {
      if (profile.chipset.empty()) {
        if (!generic_match) {
          generic_match = &profile;
        }
      } else if (equal_after_uppercase(profile.chipset, chipset)) {
        return &profile;
      }
      if (!vendor_device_match) {
        vendor_device_match = &profile;
      }
    }
  }
  if (generic_match) {
    return generic_match;
  }
  return vendor_device_match;
}

std::unordered_map<std::string, WifiTxPowerOverride> load_tx_power_overrides() {
  std::unordered_map<std::string, WifiTxPowerOverride> overrides;
  std::ifstream file(kTxPowerOverridesPath);
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
    auto key = trim_copy(line.substr(0, pos));
    auto value = trim_copy(line.substr(pos + 1));
    if (key.empty()) {
      continue;
    }
    auto dot = key.find('.');
    if (dot == std::string::npos) {
      continue;
    }
    auto iface = trim_copy(key.substr(0, dot));
    auto field = trim_copy(key.substr(dot + 1));
    if (iface.empty() || field.empty()) {
      continue;
    }
    auto field_upper = to_upper(field);
    auto& entry = overrides[iface];
    if (field_upper == "TX_POWER") {
      entry.tx_power = value;
    } else if (field_upper == "TX_POWER_HIGH") {
      entry.tx_power_high = value;
    } else if (field_upper == "TX_POWER_LOW") {
      entry.tx_power_low = value;
    } else if (field_upper == "CARD_NAME") {
      entry.card_name = value;
    } else if (field_upper == "POWER_LEVEL") {
      entry.power_level = value;
    } else if (field_upper == "PROFILE_VENDOR_ID") {
      entry.profile_vendor_id = normalize_id(value);
    } else if (field_upper == "PROFILE_DEVICE_ID") {
      entry.profile_device_id = normalize_id(value);
    } else if (field_upper == "PROFILE_CHIPSET") {
      entry.profile_chipset = normalize_chipset(value);
    }
  }
  return overrides;
}

bool write_tx_power_overrides(
    const std::unordered_map<std::string, WifiTxPowerOverride>& data) {
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(kTxPowerOverridesPath).parent_path(), ec);
  if (ec) {
    return false;
  }
  std::ofstream file(kTxPowerOverridesPath);
  if (!file) {
    return false;
  }
  file << "# OpenHD SysUtils Wi-Fi TX power overrides\n";
  for (const auto& entry : data) {
    if (!has_tx_power_values(entry.second)) {
      continue;
    }
    const auto& iface = entry.first;
    const auto& values = entry.second;
    if (!values.card_name.empty()) {
      file << iface << ".card_name=" << values.card_name << "\n";
    }
    if (!values.power_level.empty()) {
      file << iface << ".power_level=" << values.power_level << "\n";
    }
    if (!values.profile_vendor_id.empty()) {
      file << iface << ".profile_vendor_id=" << values.profile_vendor_id << "\n";
    }
    if (!values.profile_device_id.empty()) {
      file << iface << ".profile_device_id=" << values.profile_device_id << "\n";
    }
    if (!values.profile_chipset.empty()) {
      file << iface << ".profile_chipset=" << values.profile_chipset << "\n";
    }
    if (!values.tx_power.empty()) {
      file << iface << ".tx_power=" << values.tx_power << "\n";
    }
    if (!values.tx_power_high.empty()) {
      file << iface << ".tx_power_high=" << values.tx_power_high << "\n";
    }
    if (!values.tx_power_low.empty()) {
      file << iface << ".tx_power_low=" << values.tx_power_low << "\n";
    }
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

std::vector<WifiCardProfile> default_wifi_card_profiles() {
  std::vector<WifiCardProfile> profiles;

  WifiCardProfile rpi{};
  rpi.vendor_id = normalize_id("0x02D0");
  rpi.device_id = normalize_id("0xA9A6");
  rpi.chipset = normalize_chipset("BROADCOM");
  rpi.name = "Raspberry Internal";
  rpi.power_mode = "FIXED";
  profiles.push_back(rpi);

  WifiCardProfile lb{};
  lb.vendor_id = normalize_id("0x0BDA");
  lb.device_id = normalize_id("0xA81A");
  lb.chipset = normalize_chipset("OPENHD_RTL_88X2EU");
  lb.name = "LB-Link 8812eu";
  lb.power_mode = "MW";
  lb.min_mw = 25;
  lb.max_mw = 1000;
  lb.lowest_mw = 25;
  lb.low_mw = 100;
  lb.mid_mw = 500;
  lb.high_mw = 1000;
  profiles.push_back(lb);

  return profiles;
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

void fill_vendor_device_from_modalias(const std::string& modalias,
                                      std::string& vendor,
                                      std::string& device) {
  if (!vendor.empty() && !device.empty()) {
    return;
  }
  std::smatch match;
  std::regex usb_re{"usb:v([0-9A-Fa-f]{4})p([0-9A-Fa-f]{4})"};
  if (std::regex_search(modalias, match, usb_re) && match.size() == 3) {
    if (vendor.empty()) {
      vendor = normalize_id(match[1].str());
    }
    if (device.empty()) {
      device = normalize_id(match[2].str());
    }
    return;
  }
  std::regex pci_re{"pci:v([0-9A-Fa-f]{4})d([0-9A-Fa-f]{4})"};
  if (std::regex_search(modalias, match, pci_re) && match.size() == 3) {
    if (vendor.empty()) {
      vendor = normalize_id(match[1].str());
    }
    if (device.empty()) {
      device = normalize_id(match[2].str());
    }
  }
}

void fill_vendor_device_from_sysfs(const std::string& device_path,
                                   std::string& vendor,
                                   std::string& device) {
  if (device_path.empty()) {
    return;
  }
  std::filesystem::path current(device_path);
  std::error_code ec;
  auto resolved = std::filesystem::canonical(current, ec);
  if (!ec) {
    current = resolved;
  } else {
    auto weak = std::filesystem::weakly_canonical(current, ec);
    if (!ec) {
      current = weak;
    }
  }
  for (int depth = 0; depth < 6; ++depth) {
    if (current.empty()) {
      break;
    }

    const auto vendor_path = (current / "vendor").string();
    const auto device_id_path = (current / "device").string();
    const auto usb_vendor_path = (current / "idVendor").string();
    const auto usb_device_path = (current / "idProduct").string();
    const auto uevent_path = (current / "uevent").string();
    const auto modalias_path = (current / "modalias").string();

    if (vendor.empty() && file_exists(vendor_path)) {
      vendor = normalize_id(read_file(vendor_path).value_or(""));
    }
    if (device.empty() && file_exists(device_id_path)) {
      device = normalize_id(read_file(device_id_path).value_or(""));
    }
    if (vendor.empty() && file_exists(usb_vendor_path)) {
      vendor = normalize_id(read_file(usb_vendor_path).value_or(""));
    }
    if (device.empty() && file_exists(usb_device_path)) {
      device = normalize_id(read_file(usb_device_path).value_or(""));
    }
    if (file_exists(uevent_path)) {
      fill_vendor_device_from_uevent(read_file(uevent_path).value_or(""),
                                     vendor, device);
    }
    if (file_exists(modalias_path)) {
      fill_vendor_device_from_modalias(read_file(modalias_path).value_or(""),
                                       vendor, device);
    }
    if (!vendor.empty() && !device.empty()) {
      break;
    }
    current = current.parent_path();
  }
}

WifiCardInfo build_wifi_card(
    const std::string& interface_name,
    const std::unordered_map<std::string, std::string>& overrides,
    const std::unordered_map<std::string, WifiTxPowerOverride>& tx_overrides,
    const std::vector<WifiCardProfile>& profiles) {
  WifiCardInfo card{};
  card.interface_name = interface_name;

  auto device_path = "/sys/class/net/" + interface_name + "/device";
  auto uevent_path = device_path + "/uevent";
  if (interface_name == "ath0" && !file_exists(uevent_path)) {
    device_path = "/sys/class/net/wifi0/device";
    uevent_path = device_path + "/uevent";
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

  fill_vendor_device_from_sysfs(device_path, card.vendor_id, card.device_id);
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

  const auto* profile = find_wifi_profile(
      profiles, card.vendor_id, card.device_id, card.detected_type);
  auto tx_it = tx_overrides.find(interface_name);
  if (tx_it != tx_overrides.end()) {
    const auto& override_profile = tx_it->second;
    if (!override_profile.profile_vendor_id.empty() &&
        !override_profile.profile_device_id.empty()) {
      const auto& override_chipset =
          override_profile.profile_chipset.empty()
              ? card.detected_type
              : override_profile.profile_chipset;
      const auto* override_match = find_wifi_profile(
          profiles,
          override_profile.profile_vendor_id,
          override_profile.profile_device_id,
          override_chipset);
      if (!override_match) {
        override_match = find_wifi_profile(
            profiles,
            override_profile.profile_vendor_id,
            override_profile.profile_device_id,
            "");
      }
      if (override_match) {
        profile = override_match;
      }
    }
  }
  const bool profile_fixed =
      profile && to_upper(profile->power_mode) == "FIXED";
  if (profile) {
    if (card.card_name.empty()) {
      card.card_name = profile->name;
    }
    card.power_mode = profile->power_mode;
    card.power_lowest = to_string_if(profile->lowest_mw);
    card.power_low = to_string_if(profile->low_mw);
    card.power_mid = to_string_if(profile->mid_mw);
    card.power_high = to_string_if(profile->high_mw);
    card.power_min = to_string_if(profile->min_mw);
    card.power_max = to_string_if(profile->max_mw);
  }

  if (tx_it != tx_overrides.end()) {
    card.tx_power = tx_it->second.tx_power;
    card.tx_power_high = tx_it->second.tx_power_high;
    card.tx_power_low = tx_it->second.tx_power_low;
    if (!tx_it->second.card_name.empty()) {
      card.card_name = tx_it->second.card_name;
    }
    card.power_level = tx_it->second.power_level;
  }

  if (!card.power_level.empty()) {
    card.power_level = to_upper(card.power_level);
  }

  if (profile && !card.power_level.empty() && !profile_fixed) {
    const auto& level = card.power_level;
    int selected_mw = 0;
    if (level == "LOWEST") {
      selected_mw = profile->lowest_mw;
    } else if (level == "LOW") {
      selected_mw = profile->low_mw;
    } else if (level == "MID") {
      selected_mw = profile->mid_mw;
    } else if (level == "HIGH") {
      selected_mw = profile->high_mw;
    }
    if (selected_mw > 0) {
      card.tx_power = std::to_string(selected_mw);
    }
  }

  if (profile_fixed) {
    card.power_level = "FIXED";
    card.tx_power.clear();
  }

  if (card.tx_power_high.empty() && profile && profile->high_mw > 0) {
    card.tx_power_high = std::to_string(profile->high_mw);
  }
  if (card.tx_power_low.empty() && profile && profile->lowest_mw > 0) {
    card.tx_power_low = std::to_string(profile->lowest_mw);
  }

  return card;
}

std::vector<WifiCardInfo> detect_wifi_cards(
    const std::unordered_map<std::string, std::string>& overrides,
    const std::unordered_map<std::string, WifiTxPowerOverride>& tx_overrides,
    const std::vector<WifiCardProfile>& profiles) {
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
    cards.push_back(build_wifi_card(iface, overrides, tx_overrides, profiles));
  }
  return cards;
}

void refresh_wifi_info() {
  const auto overrides = load_overrides();
  const auto tx_overrides = load_tx_power_overrides();
  const auto profiles = load_wifi_card_profiles();
  g_wifi_cards = detect_wifi_cards(overrides, tx_overrides, profiles);
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
  const auto tx_power = extract_string_field(line, "tx_power");
  const auto tx_power_high = extract_string_field(line, "tx_power_high");
  const auto tx_power_low = extract_string_field(line, "tx_power_low");
  const auto card_name = extract_string_field(line, "card_name");
  const auto power_level = extract_string_field(line, "power_level");
  const auto profile_vendor_id =
      extract_string_field(line, "profile_vendor_id");
  const auto profile_device_id =
      extract_string_field(line, "profile_device_id");
  const auto profile_chipset =
      extract_string_field(line, "profile_chipset");

  bool ok = true;
  auto overrides = load_overrides();
  auto tx_overrides = load_tx_power_overrides();

  if (action == "set") {
    if (!iface || iface->empty()) {
      ok = false;
    } else {
      if (override_type.has_value()) {
        if (override_type->empty() ||
            equal_after_uppercase(*override_type, "AUTO")) {
          overrides.erase(*iface);
        } else {
          overrides[*iface] = *override_type;
        }
        ok = write_overrides(overrides) && ok;
      }

      if (tx_power.has_value() || tx_power_high.has_value() ||
          tx_power_low.has_value() || card_name.has_value() ||
          power_level.has_value() || profile_vendor_id.has_value() ||
          profile_device_id.has_value() || profile_chipset.has_value()) {
        auto& entry = tx_overrides[*iface];
        if (tx_power.has_value()) {
          entry.tx_power = *tx_power;
        }
        if (tx_power_high.has_value()) {
          entry.tx_power_high = *tx_power_high;
        }
        if (tx_power_low.has_value()) {
          entry.tx_power_low = *tx_power_low;
        }
        if (card_name.has_value()) {
          entry.card_name = *card_name;
        }
        if (power_level.has_value()) {
          if (power_level->empty() ||
              equal_after_uppercase(*power_level, "AUTO")) {
            entry.power_level.clear();
          } else {
            entry.power_level = to_upper(trim_copy(*power_level));
          }
          entry.tx_power.clear();
          entry.tx_power_high.clear();
          entry.tx_power_low.clear();
        }
        if (profile_vendor_id.has_value() ||
            profile_device_id.has_value() ||
            profile_chipset.has_value()) {
          const auto vendor_value = profile_vendor_id.value_or("");
          const auto device_value = profile_device_id.value_or("");
          const auto chipset_value = profile_chipset.value_or("");
          if (vendor_value.empty() || device_value.empty()) {
            entry.profile_vendor_id.clear();
            entry.profile_device_id.clear();
            entry.profile_chipset.clear();
          } else {
            entry.profile_vendor_id = normalize_id(vendor_value);
            entry.profile_device_id = normalize_id(device_value);
            entry.profile_chipset = normalize_chipset(chipset_value);
          }
        }
        if (!has_tx_power_values(entry)) {
          tx_overrides.erase(*iface);
        }
        ok = write_tx_power_overrides(tx_overrides) && ok;
      }
    }
  } else if (action == "clear") {
    if (iface && !iface->empty()) {
      overrides.erase(*iface);
      tx_overrides.erase(*iface);
      ok = write_overrides(overrides) && write_tx_power_overrides(tx_overrides);
    } else {
      overrides.clear();
      tx_overrides.clear();
      ok = write_overrides(overrides) && write_tx_power_overrides(tx_overrides);
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

bool is_link_control_request(const std::string& line) {
  auto type = extract_string_field(line, "type");
  return type.has_value() && *type == "sysutil.link.control";
}

std::string handle_link_control_request(const std::string& line) {
  const auto iface = extract_string_field(line, "interface");
  const auto frequency = extract_int_field(line, "frequency_mhz");
  const auto channel_width = extract_int_field(line, "channel_width_mhz");
  const auto mcs_index = extract_int_field(line, "mcs_index");
  const auto tx_power_mw = extract_int_field(line, "tx_power_mw");
  const auto tx_power_index = extract_int_field(line, "tx_power_index");
  const auto power_level = extract_string_field(line, "power_level");

  std::cerr << "[sysutils] link.control request iface="
            << (iface ? *iface : "")
            << " freq=" << (frequency ? std::to_string(*frequency) : "")
            << " width=" << (channel_width ? std::to_string(*channel_width) : "")
            << " mcs=" << (mcs_index ? std::to_string(*mcs_index) : "")
            << " tx_mw=" << (tx_power_mw ? std::to_string(*tx_power_mw) : "")
            << " tx_idx="
            << (tx_power_index ? std::to_string(*tx_power_index) : "")
            << " level=" << (power_level ? *power_level : "") << std::endl;

  bool has_value = false;
  has_value = has_value || (iface.has_value() && !iface->empty());
  has_value = has_value || frequency.has_value();
  has_value = has_value || channel_width.has_value();
  has_value = has_value || mcs_index.has_value();
  has_value = has_value || tx_power_mw.has_value();
  has_value = has_value || tx_power_index.has_value();
  has_value = has_value || (power_level.has_value() && !power_level->empty());

  bool ok = false;
  std::string message;

  if (!has_value) {
    ok = false;
    message = "No RF values provided.";
  } else if (channel_width.has_value() && *channel_width == 40) {
    ok = false;
    message = "40 MHz channel width is disabled.";
  } else {
    std::ostringstream request;
    request << "{\"type\":\"openhd.link.control\"";
    if (iface && !iface->empty()) {
      request << ",\"interface\":\"" << json_escape(*iface) << "\"";
    }
    if (frequency.has_value()) {
      request << ",\"frequency_mhz\":" << *frequency;
    }
    if (channel_width.has_value()) {
      request << ",\"channel_width_mhz\":" << *channel_width;
    }
    if (mcs_index.has_value()) {
      request << ",\"mcs_index\":" << *mcs_index;
    }
    if (tx_power_mw.has_value()) {
      request << ",\"tx_power_mw\":" << *tx_power_mw;
    }
    if (tx_power_index.has_value()) {
      request << ",\"tx_power_index\":" << *tx_power_index;
    }
    if (power_level.has_value()) {
      const auto trimmed = trim_copy(*power_level);
      if (!trimmed.empty()) {
        request << ",\"power_level\":\"" << json_escape(trimmed) << "\"";
      }
    }
    request << "}\n";

    const auto response = send_openhd_control(request.str());
    if (!response) {
      ok = false;
      message = "OpenHD control socket not available.";
      std::cerr << "[sysutils] link.control openhd response: <none>"
                << std::endl;
    } else {
      ok = extract_bool_field(*response, "ok").value_or(false);
      message = extract_string_field(*response, "message").value_or("");
      if (message.empty() && !ok) {
        message = "OpenHD rejected the RF update.";
      }
      std::cerr << "[sysutils] link.control openhd response: " << *response
                << std::endl;
    }
  }

  std::ostringstream out;
  out << "{\"type\":\"sysutil.link.control.response\",\"ok\":"
      << (ok ? "true" : "false");
  if (!message.empty()) {
    out << ",\"message\":\"" << json_escape(message) << "\"";
  }
  out << "}\n";
  return out.str();
}

}  // namespace sysutil
