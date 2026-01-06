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

#include "sysutil_protocol.h"
#include "platforms_generated.h"

namespace sysutil {
namespace {

PlatformInfo g_platform_info{};
bool g_platform_initialized = false;

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

bool contains_after_uppercase(const std::string& haystack,
                              const std::string& needle) {
  return to_upper(haystack).find(to_upper(needle)) != std::string::npos;
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

std::optional<std::string> run_command_out(const char* command);

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

void write_platform_manifest(const PlatformInfo& info) {
  static constexpr const char* kManifestFile = "/tmp/platform_manifest.txt";
  std::ofstream file(kManifestFile);
  if (!file) {
    return;
  }
  file << "OHDPlatform:[" << info.platform_name << "]";
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

}  // namespace

void init_platform_info() {
  if (g_platform_initialized) {
    return;
  }
  g_platform_info.platform_type = discover_platform_type();
  g_platform_info.platform_name =
      platform_type_to_string(g_platform_info.platform_type);
  write_platform_manifest(g_platform_info);
  g_platform_initialized = true;
}

const PlatformInfo& platform_info() {
  if (!g_platform_initialized) {
    init_platform_info();
  }
  return g_platform_info;
}

bool is_platform_request(const std::string& line) {
  auto type = extract_string_field(line, "type");
  return type.has_value() && *type == "sysutil.platform.request";
}

std::string build_platform_response() {
  const auto& info = platform_info();
  std::ostringstream out;
  out << "{\"type\":\"sysutil.platform.response\",\"platform_type\":"
      << info.platform_type << ",\"platform_name\":\""
      << json_escape(info.platform_name) << "\"}\n";
  return out.str();
}

}  // namespace sysutil
