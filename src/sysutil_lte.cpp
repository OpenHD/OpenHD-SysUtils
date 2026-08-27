#include "sysutil_lte.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>
#include <cerrno>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sysutil_config.h"

namespace sysutil {
namespace {
constexpr const char* kDefaultProfile = "/etc/wireguard/openhd-lte.conf";
std::mutex g_mutex;
LteProfile g_status;

std::string trim(std::string value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
  return first < last ? std::string(first, last) : std::string{};
}

bool valid_id(const std::string& value) {
  return !value.empty() && value.size() <= 80 &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return std::isalnum(c) || c == '-' || c == '_';
         });
}

bool valid_host(const std::string& value) {
  return !value.empty() && value.size() <= 253 &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return std::isalnum(c) || c == '.' || c == ':' || c == '-';
         });
}

int parse_port(const std::string& value) {
  try {
    std::size_t consumed = 0;
    const int port = std::stoi(value, &consumed);
    return consumed == value.size() && port > 0 && port <= 65535 ? port : 0;
  } catch (...) {
    return 0;
  }
}

std::string metadata(const std::string& line, const std::string& key) {
  const std::string prefix = "# OpenHD-" + key + "=";
  return line.rfind(prefix, 0) == 0 ? trim(line.substr(prefix.size())) : std::string{};
}

int run(const char* program, const std::initializer_list<std::string>& arguments) {
  const pid_t child = fork();
  if (child < 0) return -1;
  if (child == 0) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(program));
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    execvp(program, argv.data());
    _exit(127);
  }
  int status = 0;
  pid_t waited = -1;
  do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
  if (waited < 0) return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
}  // namespace

LteProfile load_lte_profile(const std::string& path) {
  LteProfile result;
  std::ifstream input(path);
  if (!input) return result;

  std::string line;
  while (std::getline(input, line)) {
    if (auto value = metadata(line, "Device-ID"); !value.empty()) result.device_id = value;
    if (auto value = metadata(line, "FleetControl-Address"); !value.empty()) result.fleetcontrol_address = value;
    if (auto value = metadata(line, "Interface"); !value.empty()) result.interface_name = value;
    if (auto value = metadata(line, "Video-Port"); !value.empty()) result.video_port = parse_port(value);
    if (auto value = metadata(line, "Video2-Port"); !value.empty()) result.video2_port = parse_port(value);
    if (auto value = metadata(line, "Telemetry-Port"); !value.empty()) result.telemetry_port = parse_port(value);
  }
  const auto profile_interface = std::filesystem::path(path).stem().string();
  if (result.interface_name.empty()) result.interface_name = profile_interface;
  result.configured = valid_id(result.device_id) && valid_host(result.fleetcontrol_address) &&
                      valid_id(result.interface_name) && result.interface_name == profile_interface &&
                      result.video_port != 0 &&
                      result.video2_port != 0 && result.telemetry_port != 0;
  return result;
}

LteProfile initialize_lte_link() {
  SysutilConfig config;
  (void)load_sysutil_config(config);
  LteProfile status;
  if (config.lte_enabled.value_or(false)) {
    const auto path = config.lte_wireguard_config.value_or(kDefaultProfile);
    status = load_lte_profile(path);
    if (status.configured) {
      struct stat profile_stat {};
      const bool private_permissions =
          stat(path.c_str(), &profile_stat) == 0 &&
          profile_stat.st_uid == 0 && (profile_stat.st_mode & 0077) == 0;
      status.configured = private_permissions;
      status.active = private_permissions &&
                      (run("wg", {"show", status.interface_name}) == 0 ||
                       run("wg-quick", {"up", path}) == 0);
    }
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_status = status;
  return g_status;
}

LteProfile lte_profile_status() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_status;
}
}  // namespace sysutil
