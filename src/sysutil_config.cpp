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
  config.firstboot = extract_bool_field(content, "firstboot");
  config.init_system = extract_string_field(content, "init_system");
  config.shell = extract_string_field(content, "shell");
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
  if (config.platform_type) {
    file << "  \"platform_type\": " << *config.platform_type;
    wrote_field = true;
  }
  if (config.platform_name) {
    if (wrote_field) {
      file << ",\n";
    }
    file << "  \"platform_name\": \""
         << json_escape(*config.platform_name) << "\"";
    wrote_field = true;
  }
  if (config.debug_enabled) {
    if (wrote_field) {
      file << ",\n";
    }
    file << "  \"debug\": " << (*config.debug_enabled ? "true" : "false");
    wrote_field = true;
  }
  if (config.firstboot) {
    if (wrote_field) {
      file << ",\n";
    }
    file << "  \"firstboot\": " << (*config.firstboot ? "true" : "false");
    wrote_field = true;
  }
  if (config.init_system) {
    if (wrote_field) {
      file << ",\n";
    }
    file << "  \"init_system\": \""
         << json_escape(*config.init_system) << "\"";
    wrote_field = true;
  }
  if (config.shell) {
    if (wrote_field) {
      file << ",\n";
    }
    file << "  \"shell\": \"" << json_escape(*config.shell) << "\"";
  }
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
