/******************************************************************************
 * OpenHD - display mode configuration
 ******************************************************************************/

#include "sysutil_display.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

#include "sysutil_config.h"
#include "sysutil_protocol.h"
#include "sysutil_status.h"

namespace sysutil {
namespace {

constexpr const char* kDefaultConnector = "HDMI-A-1";
constexpr const char* kExtlinuxPath = "/boot/extlinux/extlinux.conf";
constexpr const char* kGlideConfigPath = "/etc/default/openhd-glide";
constexpr const char* kExtlinuxMarker = "# OpenHD forced display mode: ";

bool read_file(const std::string& path, std::string& content) {
  std::ifstream file(path);
  if (!file) return false;
  std::ostringstream out;
  out << file.rdbuf();
  content = out.str();
  return true;
}

bool write_if_changed(const std::string& path, const std::string& content,
                      bool& changed) {
  std::string old;
  if (read_file(path, old) && old == content) return true;
  std::ofstream file(path, std::ios::trunc);
  if (!file) return false;
  file << content;
  if (!file) return false;
  changed = true;
  return true;
}

std::string trim(std::string value) {
  const auto non_space = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), non_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), non_space).base(),
              value.end());
  return value;
}

bool valid_mode(int width, int height, int refresh_hz) {
  return width >= 320 && width <= 7680 && height >= 240 && height <= 4320 &&
         refresh_hz >= 20 && refresh_hz <= 240;
}

bool valid_connector(const std::string& connector) {
  static const std::regex pattern("^[A-Za-z0-9-]{1,32}$");
  return std::regex_match(connector, pattern);
}

std::string video_argument(const SysutilConfig& config) {
  if (!config.display_force_mode.value_or(false)) return "";
  const int width = config.display_width.value_or(0);
  const int height = config.display_height.value_or(0);
  const int refresh = config.display_refresh_hz.value_or(0);
  const std::string connector =
      config.display_connector.value_or(kDefaultConnector);
  if (!valid_mode(width, height, refresh) || !valid_connector(connector)) return "";
  return "video=" + connector + ":" + std::to_string(width) + "x" +
         std::to_string(height) + "@" + std::to_string(refresh) + "D";
}

std::vector<std::string> split_lines(const std::string& content) {
  std::vector<std::string> lines;
  std::istringstream input(content);
  std::string line;
  while (std::getline(input, line)) lines.push_back(line);
  return lines;
}

std::string join_lines(const std::vector<std::string>& lines) {
  std::ostringstream out;
  for (const auto& line : lines) out << line << '\n';
  return out.str();
}

void remove_argument(std::string& line, const std::string& argument) {
  if (argument.empty()) return;
  std::size_t offset = 0;
  while ((offset = line.find(argument, offset)) != std::string::npos) {
    const bool left_boundary =
        offset == 0 || std::isspace(static_cast<unsigned char>(line[offset - 1]));
    const std::size_t end = offset + argument.size();
    const bool right_boundary =
        end == line.size() || std::isspace(static_cast<unsigned char>(line[end]));
    if (left_boundary && right_boundary) {
      line.erase(offset, argument.size());
    } else {
      offset = end;
    }
  }
}

bool update_extlinux(const std::string& wanted, bool& changed) {
  std::string content;
  if (!read_file(kExtlinuxPath, content)) return true;
  auto lines = split_lines(content);
  std::string previous;
  for (const auto& line : lines) {
    const auto marker = line.find(kExtlinuxMarker);
    if (marker != std::string::npos) {
      previous = trim(line.substr(marker + std::string(kExtlinuxMarker).size()));
      break;
    }
  }

  std::vector<std::string> output;
  for (auto line : lines) {
    if (line.find(kExtlinuxMarker) != std::string::npos) continue;
    remove_argument(line, previous);
    std::string lowered = line;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!wanted.empty() && trim(lowered).rfind("append", 0) == 0) {
      line = trim(line) + " " + wanted;
    }
    output.push_back(line);
  }
  if (!wanted.empty()) output.push_back(std::string(kExtlinuxMarker) + wanted);
  return write_if_changed(kExtlinuxPath, join_lines(output), changed);
}

bool update_cmdline(const std::string& path, const std::string& wanted,
                    bool& changed) {
  std::string content;
  if (!read_file(path, content)) return true;
  std::istringstream input(content);
  std::vector<std::string> tokens;
  std::string token;
  static const std::regex hdmi_mode("^video=HDMI-A-[0-9]+:.*$");
  while (input >> token) {
    if (!std::regex_match(token, hdmi_mode)) tokens.push_back(token);
  }
  if (!wanted.empty()) tokens.push_back(wanted);
  std::ostringstream output;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (i) output << ' ';
    output << tokens[i];
  }
  output << '\n';
  return write_if_changed(path, output.str(), changed);
}

bool replace_glide_value(std::vector<std::string>& lines,
                         const std::string& key, const std::string& value) {
  const std::string prefix = key + "=";
  for (auto& line : lines) {
    if (line.rfind(prefix, 0) == 0) {
      const std::string replacement = prefix + value;
      if (line == replacement) return false;
      line = replacement;
      return true;
    }
  }
  lines.push_back(prefix + value);
  return true;
}

bool update_glide(const SysutilConfig& config, bool& changed) {
  std::string content;
  std::vector<std::string> lines;
  if (read_file(kGlideConfigPath, content)) lines = split_lines(content);
  const bool force = config.display_force_mode.value_or(false);
  replace_glide_value(lines, "GLIDE_WIDTH",
                      force ? std::to_string(config.display_width.value_or(0)) : "auto");
  replace_glide_value(lines, "GLIDE_HEIGHT",
                      force ? std::to_string(config.display_height.value_or(0)) : "auto");
  replace_glide_value(lines, "GLIDE_DISPLAY_HZ",
                      force ? std::to_string(config.display_refresh_hz.value_or(0)) : "0");
  return write_if_changed(kGlideConfigPath, join_lines(lines), changed);
}

bool apply_display_config(bool& changed) {
  SysutilConfig config;
  if (load_sysutil_config(config) == ConfigLoadResult::Error) return false;
  // Older images/configs do not contain this setting. In that case leave all
  // existing boot and Glide configuration untouched.
  if (!config.display_force_mode.has_value()) return true;
  const std::string wanted = video_argument(config);
  if (config.display_force_mode.value_or(false) && wanted.empty()) return false;

  if (!update_extlinux(wanted, changed) ||
      !update_cmdline("/boot/firmware/cmdline.txt", wanted, changed) ||
      !update_cmdline("/boot/cmdline.txt", wanted, changed) ||
      !update_glide(config, changed)) {
    set_status("display_setup", "Display setup failed",
               "Unable to update the boot or Glide display configuration.", 2);
    return false;
  }
  if (changed) {
    set_status("display_setup", "Display settings applied",
               wanted.empty() ? "Automatic display mode restored."
                              : "Forced display mode configured.");
  }
  return true;
}

}  // namespace

bool apply_display_config_if_needed() {
  bool changed = false;
  return apply_display_config(changed) && changed;
}

bool is_display_request(const std::string& line) {
  const auto type = extract_string_field(line, "type");
  return type && *type == "sysutil.display.request";
}

bool is_display_update_request(const std::string& line) {
  const auto type = extract_string_field(line, "type");
  return type && *type == "sysutil.display.update";
}

std::string build_display_response() {
  SysutilConfig config;
  if (load_sysutil_config(config) == ConfigLoadResult::Error)
    return "{\"type\":\"sysutil.display.response\",\"ok\":false}\n";
  std::ostringstream out;
  out << "{\"type\":\"sysutil.display.response\",\"ok\":true"
      << ",\"enabled\":" << (config.display_force_mode.value_or(false) ? "true" : "false")
      << ",\"width\":" << config.display_width.value_or(1920)
      << ",\"height\":" << config.display_height.value_or(1080)
      << ",\"refresh_hz\":" << config.display_refresh_hz.value_or(60)
      << ",\"connector\":\"" << config.display_connector.value_or(kDefaultConnector)
      << "\"}\n";
  return out.str();
}

std::string handle_display_update(const std::string& line) {
  const bool enabled = extract_bool_field(line, "enabled").value_or(false);
  const int width = extract_int_field(line, "width").value_or(0);
  const int height = extract_int_field(line, "height").value_or(0);
  const int refresh = extract_int_field(line, "refresh_hz").value_or(0);
  const std::string connector =
      extract_string_field(line, "connector").value_or(kDefaultConnector);
  if (enabled && (!valid_mode(width, height, refresh) || !valid_connector(connector))) {
    return "{\"type\":\"sysutil.display.update.response\",\"ok\":false,\"message\":\"invalid display mode\"}\n";
  }

  SysutilConfig config;
  if (load_sysutil_config(config) == ConfigLoadResult::Error)
    return "{\"type\":\"sysutil.display.update.response\",\"ok\":false}\n";
  config.display_force_mode = enabled;
  if (enabled) {
    config.display_width = width;
    config.display_height = height;
    config.display_refresh_hz = refresh;
    config.display_connector = connector;
  }
  if (!write_sysutil_config(config))
    return "{\"type\":\"sysutil.display.update.response\",\"ok\":false,\"message\":\"config write failed\"}\n";

  bool changed = false;
  if (!apply_display_config(changed)) {
    return "{\"type\":\"sysutil.display.update.response\",\"ok\":false,\"message\":\"display files update failed\"}\n";
  }

  // Respond before rebooting so the Web UI can show a useful result.
  std::thread([]() {
    set_status("reboot", "Reboot initiated",
               "Rebooting after display configuration.");
    std::this_thread::sleep_for(std::chrono::milliseconds(750));
    std::system("reboot");
  }).detach();
  return "{\"type\":\"sysutil.display.update.response\",\"ok\":true,\"rebooting\":true}\n";
}

}  // namespace sysutil
