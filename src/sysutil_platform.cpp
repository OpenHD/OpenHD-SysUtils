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

#include "sysutil_platform.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_map>

#include "sysutil_config.h"
#include "sysutil_protocol.h"
#include "platforms_generated.h"

namespace sysutil {
namespace {

// Cached platform information for this process.
PlatformInfo g_platform_info{};
bool g_platform_initialized = false;

// Checks for file presence with a non-throwing API.
bool file_exists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

// Reads a file to string or returns nullopt if unavailable.
std::optional<std::string> read_file(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    return std::nullopt;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// Uppercases a string for case-insensitive comparisons.
std::string to_upper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return value;
}

// Checks for a substring after uppercasing both sides.
bool contains_after_uppercase(const std::string& haystack,
                              const std::string& needle) {
  return to_upper(haystack).find(to_upper(needle)) != std::string::npos;
}

// Checks for a substring with case sensitivity.
bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

std::optional<std::string> run_command_out(const char* command);

// Reads a file once and caches its content for reuse.
const std::optional<std::string>& read_file_cached(
    const char* path,
    std::unordered_map<std::string, std::optional<std::string>>& cache) {
  auto it = cache.find(path);
  if (it != cache.end()) {
    return it->second;
  }
  auto value = read_file(path);
  auto result = cache.emplace(path, std::move(value));
  return result.first->second;
}

// Checks if a regex matches, optionally enforcing capture equality.
bool regex_matches(const std::string& content,
                   const char* pattern,
                   const char* group_equals,
                   bool case_insensitive) {
  const auto flags = case_insensitive ? std::regex::icase : std::regex::ECMAScript;
  std::regex re(pattern, flags);
  std::smatch match;
  if (!std::regex_search(content, match, re)) {
    return false;
  }
  if (group_equals && group_equals[0] != '\0') {
    if (match.size() < 2) {
      return false;
    }
    if (case_insensitive) {
      return to_upper(match[1].str()) == to_upper(group_equals);
    }
    return match[1].str() == group_equals;
  }
  return true;
}

// Evaluates a single detection condition against cached data.
bool condition_matches(
    const DetectionCondition& condition,
    std::unordered_map<std::string, std::optional<std::string>>& cache,
    std::optional<std::string>& arch_cache) {
  switch (condition.kind) {
    case ConditionKind::FileExists:
      return file_exists(condition.path);
    case ConditionKind::FileContainsAny: {
      const auto& content = read_file_cached(condition.path, cache);
      if (!content) {
        return false;
      }
      for (std::size_t i = 0; i < condition.value_count; ++i) {
        const char* value = condition.values[i];
        if (condition.case_insensitive) {
          if (contains_after_uppercase(*content, value)) {
            return true;
          }
        } else if (contains(*content, value)) {
          return true;
        }
      }
      return false;
    }
    case ConditionKind::FileRegex: {
      const auto& content = read_file_cached(condition.path, cache);
      if (!content) {
        return false;
      }
      return regex_matches(*content, condition.pattern, condition.group_equals,
                           condition.case_insensitive);
    }
    case ConditionKind::ArchRegex: {
      if (!arch_cache.has_value()) {
        arch_cache = run_command_out("arch");
      }
      if (!arch_cache.has_value()) {
        return false;
      }
      return regex_matches(*arch_cache, condition.pattern, nullptr,
                           condition.case_insensitive);
    }
  }
  return false;
}

// Runs a command and captures stdout as a single line.
std::optional<std::string> run_command_out(const char* command) {
  FILE* pipe = popen(command, "r");
  if (!pipe) {
    return std::nullopt;
  }
  std::string output;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe)) {
    output += buffer;
  }
  const int status = pclose(pipe);
  if (status == -1) {
    return std::nullopt;
  }
  output.erase(std::remove(output.begin(), output.end(), '\n'), output.end());
  return output;
}

// Applies detection rules from platforms_generated.h to choose a platform.
int discover_platform_type() {
  std::cout << "OpenHD Platform Discovery started." << std::endl;
  std::unordered_map<std::string, std::optional<std::string>> file_cache;
  std::optional<std::string> arch_cache;

  for (const auto& rule : kDetectionRules) {
    bool matches = true;
    for (std::size_t i = 0; i < rule.condition_count; ++i) {
      if (!condition_matches(rule.conditions[i], file_cache, arch_cache)) {
        matches = false;
        break;
      }
    }
    if (matches) {
      if (rule.log && rule.log[0] != '\0') {
        std::cout << rule.log << std::endl;
      }
      return rule.platform_id;
    }
  }

  std::cout << "Unknown platform." << std::endl;
  return X_PLATFORM_TYPE_UNKNOWN;
}

// Writes a small manifest used by other components.
void write_platform_manifest(const PlatformInfo& info) {
  static constexpr const char* kManifestFile = "/tmp/platform_manifest.txt";
  std::ofstream file(kManifestFile);
  if (!file) {
    return;
  }
  file << "OHDPlatform:[" << info.platform_name << "]";
}

// Escapes JSON payload content.
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

// Returns a freshly detected platform info snapshot.
PlatformInfo discover_platform_info() {
  PlatformInfo info;
  info.platform_type = discover_platform_type();
  info.platform_name = platform_type_to_string(info.platform_type);
  return info;
}

// Initializes cached platform info from config or discovery.
void init_platform_info() {
  if (g_platform_initialized) {
    return;
  }
  SysutilConfig config;
  const auto load_result = load_sysutil_config(config);

  const bool has_cached_platform =
      (load_result == ConfigLoadResult::Loaded &&
       config.platform_type.has_value() && config.platform_name.has_value());

  if (load_result == ConfigLoadResult::Loaded && config.platform_type) {
    g_platform_info.platform_type = *config.platform_type;
  } else {
    g_platform_info.platform_type = discover_platform_type();
  }

  if (load_result == ConfigLoadResult::Loaded && config.platform_name) {
    g_platform_info.platform_name = *config.platform_name;
  } else {
    g_platform_info.platform_name =
        platform_type_to_string(g_platform_info.platform_type);
  }

  if (!has_cached_platform && load_result != ConfigLoadResult::Error) {
    SysutilConfig updated_config = config;
    updated_config.platform_type = g_platform_info.platform_type;
    updated_config.platform_name = g_platform_info.platform_name;
    (void)write_sysutil_config(updated_config);
  }
  write_platform_manifest(g_platform_info);
  g_platform_initialized = true;
}

// Returns cached platform info, initializing on first access.
const PlatformInfo& platform_info() {
  if (!g_platform_initialized) {
    init_platform_info();
  }
  return g_platform_info;
}

// Checks whether a request asks for platform info.
bool is_platform_request(const std::string& line) {
  auto type = extract_string_field(line, "type");
  return type.has_value() && *type == "sysutil.platform.request";
}

// Builds JSON response for platform requests.
std::string build_platform_response() {
  const auto& info = platform_info();
  std::ostringstream out;
  out << "{\"type\":\"sysutil.platform.response\",\"platform_type\":"
      << info.platform_type << ",\"platform_name\":\""
      << json_escape(info.platform_name) << "\"}\n";
  return out.str();
}

// Checks whether a request asks to update or refresh platform info.
bool is_platform_update_request(const std::string& line) {
  auto type = extract_string_field(line, "type");
  return type.has_value() && *type == "sysutil.platform.update";
}

// Handles platform update requests (refresh detection or override).
std::string handle_platform_update(const std::string& line) {
  auto action = extract_string_field(line, "action").value_or("refresh");
  SysutilConfig config;
  const auto load_result = load_sysutil_config(config);
  if (load_result == ConfigLoadResult::Error) {
    return "{\"type\":\"sysutil.platform.update.response\",\"ok\":false}\n";
  }

  bool ok = true;
  PlatformInfo info = platform_info();

  if (action == "set") {
    auto platform_type = extract_int_field(line, "platform_type");
    auto platform_name = extract_string_field(line, "platform_name");
    if (!platform_type.has_value()) {
      ok = false;
    } else {
      info.platform_type = *platform_type;
      if (platform_name) {
        info.platform_name = *platform_name;
      } else {
        info.platform_name = platform_type_to_string(info.platform_type);
      }
      config.platform_type = info.platform_type;
      config.platform_name = info.platform_name;
      ok = write_sysutil_config(config);
    }
  } else if (action == "clear" || action == "refresh" || action == "detect") {
    config.platform_type = std::nullopt;
    config.platform_name = std::nullopt;
    info = discover_platform_info();
    config.platform_type = info.platform_type;
    config.platform_name = info.platform_name;
    ok = write_sysutil_config(config);
  } else {
    ok = false;
  }

  if (ok) {
    g_platform_info = info;
    g_platform_initialized = true;
    write_platform_manifest(g_platform_info);
  }

  std::ostringstream out;
  out << "{\"type\":\"sysutil.platform.update.response\",\"ok\":"
      << (ok ? "true" : "false")
      << ",\"platform_type\":" << info.platform_type
      << ",\"platform_name\":\"" << json_escape(info.platform_name)
      << "\",\"action\":\"" << json_escape(action) << "\"}\n";
  return out.str();
}

}  // namespace sysutil
