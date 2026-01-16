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
 * ЖИ OpenHD, All Rights Reserved.
 ******************************************************************************/

#include "sysutil_update.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "sysutil_protocol.h"
#include "sysutil_status.h"

namespace sysutil {
namespace {

constexpr int kUpdatePollSeconds = 4;
constexpr int kStableSeconds = 3;
constexpr int kFailureBackoffSeconds = 30;

std::atomic<bool> g_update_requested{false};
std::atomic<bool> g_updating{false};
std::mutex g_update_mutex;
std::condition_variable g_update_cv;
std::thread g_update_thread;
std::chrono::steady_clock::time_point g_last_failure{};

struct UpdateSource {
  std::filesystem::path base_dir;
  std::filesystem::path zip_path;
  bool from_zip = false;
};

std::string trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool file_exists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

bool path_is_regular_file(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec);
}

bool is_recently_modified(const std::filesystem::path& path, int seconds) {
  std::error_code ec;
  const auto ftime = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return true;
  }
  const auto now = std::filesystem::file_time_type::clock::now();
  const auto age = now - ftime;
  return age < std::chrono::seconds(seconds);
}

std::optional<std::string> run_command_out(const std::string& command) {
  FILE* pipe = popen(command.c_str(), "r");
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
  return output;
}

bool run_shell_command(const std::string& command) {
  const int ret = std::system(command.c_str());
  return ret == 0;
}

bool command_exists(const std::string& name) {
  const std::string cmd = "command -v " + name + " >/dev/null 2>&1";
  return run_shell_command(cmd);
}

std::string escape_single_quotes(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  return out;
}

bool compare_versions(const std::string& lhs, const std::string& op,
                      const std::string& rhs) {
  if (!command_exists("dpkg")) {
    return false;
  }
  const std::string cmd = "dpkg --compare-versions '" +
                          escape_single_quotes(lhs) + "' " + op + " '" +
                          escape_single_quotes(rhs) + "'";
  return run_shell_command(cmd);
}

std::string select_log_path() {
  const std::vector<std::string> candidates = {
      "/boot/openhd/install-log.txt",
      "/Config/openhd/install-log.txt",
      "/var/log/openhd-update.log"};
  for (const auto& candidate : candidates) {
    std::filesystem::path path(candidate);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream test(path, std::ios::app);
    if (test) {
      return candidate;
    }
  }
  return "/tmp/openhd-update.log";
}

void log_line(std::ofstream& log, const std::string& line) {
  log << line << std::endl;
}

void set_update_status(const std::string& step,
                       const std::string& message,
                       int severity = 0) {
  set_status("updating", step, message, severity);
}

bool is_valid_package_name(const std::string& name) {
  if (name.empty()) {
    return false;
  }
  static const std::regex re(R"(^[A-Za-z0-9+.\-]+$)");
  return std::regex_match(name, re);
}

std::vector<std::string> read_package_list(const std::filesystem::path& path) {
  std::vector<std::string> packages;
  std::ifstream file(path);
  if (!file) {
    return packages;
  }
  std::string line;
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (is_valid_package_name(line)) {
      packages.push_back(line);
    }
  }
  return packages;
}

bool file_contents_equal(const std::filesystem::path& lhs,
                         const std::filesystem::path& rhs) {
  std::ifstream a(lhs, std::ios::binary);
  std::ifstream b(rhs, std::ios::binary);
  if (!a || !b) {
    return false;
  }
  constexpr std::size_t kBufferSize = 8192;
  char buf_a[kBufferSize];
  char buf_b[kBufferSize];
  while (a && b) {
    a.read(buf_a, kBufferSize);
    b.read(buf_b, kBufferSize);
    if (a.gcount() != b.gcount()) {
      return false;
    }
    if (std::memcmp(buf_a, buf_b, static_cast<std::size_t>(a.gcount())) != 0) {
      return false;
    }
  }
  return a.eof() && b.eof();
}

void ensure_hold_file() {
  std::error_code ec;
  std::filesystem::create_directories("/run/openhd", ec);
  std::ofstream hold("/run/openhd/hold.pid");
  hold.close();
}

void remove_hold_file() {
  std::error_code ec;
  std::filesystem::remove("/run/openhd/hold.pid", ec);
}

void stop_openhd_services() {
  if (!command_exists("systemctl")) {
    return;
  }
  const std::string cmd =
      "systemctl stop openhd.service openhd_rpi.service openhd_mod.service "
      "openhd-x20.service qopenhd.service >/dev/null 2>&1";
  (void)run_shell_command(cmd);
}

void mask_openhd_services() {
  if (!command_exists("systemctl")) {
    return;
  }
  const std::string cmd =
      "systemctl mask openhd.service openhd_rpi.service openhd_mod.service "
      "openhd-x20.service qopenhd.service >/dev/null 2>&1";
  (void)run_shell_command(cmd);
}

void unmask_openhd_services() {
  if (!command_exists("systemctl")) {
    return;
  }
  const std::string cmd =
      "systemctl unmask openhd.service openhd_rpi.service openhd_mod.service "
      "openhd-x20.service qopenhd.service >/dev/null 2>&1";
  (void)run_shell_command(cmd);
}

bool has_update_payload(const std::filesystem::path& dir) {
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec)) {
    return false;
  }
  if (file_exists(dir / "apt-packages.txt") ||
      file_exists(dir / "apt.txt") ||
      file_exists(dir / "apt_packages.txt")) {
    return true;
  }
  if (file_exists(dir / "binaries")) {
    return true;
  }
  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }
    const auto ext = to_lower(entry.path().extension().string());
    if (ext == ".deb" || ext == ".bin" || ext == ".hex") {
      return true;
    }
  }
  return false;
}

std::optional<UpdateSource> find_update_source() {
  const std::vector<std::filesystem::path> zip_candidates = {
      "/boot/openhd/update/update.zip",
      "/boot/openhd/update.zip",
      "/Config/openhd/update/update.zip",
      "/Config/openhd/update.zip",
      "/usr/local/share/openhd/update.zip"};

  for (const auto& zip : zip_candidates) {
    if (!path_is_regular_file(zip)) {
      continue;
    }
    if (is_recently_modified(zip, kStableSeconds)) {
      continue;
    }
    UpdateSource source;
    source.zip_path = zip;
    source.from_zip = true;
    source.base_dir = zip.parent_path();
    return source;
  }

  const std::vector<std::filesystem::path> dir_candidates = {
      "/boot/openhd/update",
      "/Config/openhd/update",
      "/usr/local/share/openhd/update"};

  for (const auto& dir : dir_candidates) {
    if (!has_update_payload(dir)) {
      continue;
    }
    UpdateSource source;
    source.base_dir = dir;
    return source;
  }
  return std::nullopt;
}

std::filesystem::path make_temp_dir() {
  auto base = std::filesystem::temp_directory_path();
  const auto id = std::to_string(::getpid());
  const auto timestamp = std::to_string(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  auto path = base / ("openhd_update_" + id + "_" + timestamp);
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  return path;
}

std::optional<std::filesystem::path> extract_zip(
    const std::filesystem::path& zip_path,
    std::ofstream& log) {
  if (!command_exists("unzip")) {
    log_line(log, "unzip not available; cannot extract update.zip");
    return std::nullopt;
  }
  auto temp_dir = make_temp_dir();
  const std::string cmd = "unzip -o '" + zip_path.string() + "' -d '" +
                          temp_dir.string() + "' >> '" + select_log_path() +
                          "' 2>&1";
  log_line(log, "Extracting " + zip_path.string());
  if (!run_shell_command(cmd)) {
    log_line(log, "Failed to extract update.zip");
    return std::nullopt;
  }
  return temp_dir;
}

struct AptPackageInfo {
  std::string installed;
  std::string candidate;
};

std::optional<AptPackageInfo> read_apt_policy(const std::string& package) {
  auto output = run_command_out("apt-cache policy " + package + " 2>/dev/null");
  if (!output) {
    return std::nullopt;
  }
  AptPackageInfo info;
  std::istringstream iss(*output);
  std::string line;
  while (std::getline(iss, line)) {
    const auto trimmed = trim(line);
    if (trimmed.rfind("Installed:", 0) == 0) {
      info.installed = trim(trimmed.substr(std::string("Installed:").size()));
    } else if (trimmed.rfind("Candidate:", 0) == 0) {
      info.candidate = trim(trimmed.substr(std::string("Candidate:").size()));
    }
  }
  if (info.installed.empty() && info.candidate.empty()) {
    return std::nullopt;
  }
  return info;
}

bool install_apt_packages(const std::vector<std::string>& packages,
                          std::ofstream& log) {
  if (packages.empty()) {
    return true;
  }
  if (!command_exists("apt-get") || !command_exists("apt-cache")) {
    log_line(log, "apt-get/apt-cache not available");
    return false;
  }
  set_update_status("Updating packages", "Refreshing apt metadata.");
  if (!run_shell_command("apt-get update >> '" + select_log_path() + "' 2>&1")) {
    log_line(log, "apt-get update failed");
    return false;
  }

  int updated = 0;
  for (const auto& pkg : packages) {
    auto policy = read_apt_policy(pkg);
    if (!policy) {
      log_line(log, "Skipping apt package " + pkg + " (no policy)");
      continue;
    }
    const auto installed = policy->installed;
    const auto candidate = policy->candidate;
    if (candidate.empty() || candidate == "(none)") {
      log_line(log, "Skipping apt package " + pkg + " (no candidate)");
      continue;
    }
    bool should_install = false;
    if (installed.empty() || installed == "(none)") {
      should_install = true;
    } else if (compare_versions(candidate, "gt", installed)) {
      should_install = true;
    }

    if (!should_install) {
      log_line(log, "Apt package up to date: " + pkg);
      continue;
    }
    set_update_status("Updating packages",
                      "Installing " + pkg + " (" + candidate + ").");
    const std::string cmd = "apt-get install -y " + pkg +
                            " >> '" + select_log_path() + "' 2>&1";
    if (!run_shell_command(cmd)) {
      log_line(log, "apt-get install failed for " + pkg);
      return false;
    }
    ++updated;
  }

  log_line(log, "Apt packages updated: " + std::to_string(updated));
  return true;
}

struct DebInfo {
  std::string name;
  std::string version;
};

std::optional<DebInfo> read_deb_info(const std::filesystem::path& deb_path) {
  if (!command_exists("dpkg-deb")) {
    return std::nullopt;
  }
  DebInfo info;
  auto name_out = run_command_out("dpkg-deb -f '" + deb_path.string() +
                                  "' Package 2>/dev/null");
  auto version_out = run_command_out("dpkg-deb -f '" + deb_path.string() +
                                     "' Version 2>/dev/null");
  if (!name_out || !version_out) {
    return std::nullopt;
  }
  info.name = trim(*name_out);
  info.version = trim(*version_out);
  if (info.name.empty() || info.version.empty()) {
    return std::nullopt;
  }
  return info;
}

std::optional<std::string> read_installed_version(const std::string& package) {
  if (!command_exists("dpkg-query")) {
    return std::nullopt;
  }
  auto output = run_command_out(
      "dpkg-query -W -f='${Version}' " + package + " 2>/dev/null");
  if (!output) {
    return std::nullopt;
  }
  auto value = trim(*output);
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

bool install_deb_package(const std::filesystem::path& deb_path,
                         std::ofstream& log) {
  if (!command_exists("dpkg")) {
    log_line(log, "dpkg not available; skipping " + deb_path.string());
    return false;
  }
  set_update_status("Installing packages", "Installing " + deb_path.filename().string());
  const std::string cmd = "dpkg -i --force-overwrite '" + deb_path.string() +
                          "' >> '" + select_log_path() + "' 2>&1";
  if (!run_shell_command(cmd)) {
    log_line(log, "dpkg install failed for " + deb_path.string());
    return false;
  }
  return true;
}

struct BinaryUpdate {
  std::filesystem::path source;
  std::filesystem::path target;
};

bool apply_binary_update(const BinaryUpdate& update, std::ofstream& log);

bool apply_deb_updates(const std::vector<std::filesystem::path>& debs,
                       std::ofstream& log) {
  if (debs.empty()) {
    return true;
  }
  if (!command_exists("dpkg")) {
    if (!command_exists("dpkg-deb")) {
      log_line(log, "dpkg/dpkg-deb not available; cannot install debs");
      return false;
    }
    for (const auto& deb : debs) {
      auto temp_dir = make_temp_dir();
      const std::string cmd = "dpkg-deb -x '" + deb.string() + "' '" +
                              temp_dir.string() + "' >> '" +
                              select_log_path() + "' 2>&1";
      if (!run_shell_command(cmd)) {
        log_line(log, "dpkg-deb extract failed for " + deb.string());
        return false;
      }
        const std::vector<BinaryUpdate> extracted = {
            {temp_dir / "usr/local/bin/openhd", "/usr/local/bin/openhd"},
            {temp_dir / "usr/local/bin/QOpenHD", "/usr/local/bin/QOpenHD"},
            {temp_dir / "usr/local/bin/qopenhd", "/usr/local/bin/QOpenHD"}};
        for (const auto& item : extracted) {
          if (path_is_regular_file(item.source)) {
            if (!apply_binary_update(item, log)) {
              return false;
            }
          }
      }
      std::error_code ec;
      std::filesystem::remove_all(temp_dir, ec);
    }
    return true;
  }
  for (const auto& deb : debs) {
    auto info = read_deb_info(deb);
    if (info) {
      auto installed = read_installed_version(info->name);
      if (installed && !compare_versions(info->version, "gt", *installed)) {
        log_line(log, "Deb up to date: " + info->name + " (" + *installed + ")");
        continue;
      }
    }
    if (!install_deb_package(deb, log)) {
      return false;
    }
  }
  return true;
}

std::vector<BinaryUpdate> find_binary_updates(const std::filesystem::path& base) {
  std::vector<BinaryUpdate> updates;
  const std::filesystem::path bin_dir = base / "binaries";
  const std::vector<std::pair<std::string, std::string>> candidates = {
      {"openhd", "/usr/local/bin/openhd"},
      {"qopenhd", "/usr/local/bin/QOpenHD"},
      {"QOpenHD", "/usr/local/bin/QOpenHD"},
      {"openhd_sys_utils", "/usr/local/bin/openhd_sys_utils"}};

  for (const auto& entry : candidates) {
    const auto source = bin_dir / entry.first;
    if (path_is_regular_file(source)) {
      updates.push_back({source, entry.second});
    }
  }
  return updates;
}

bool apply_binary_update(const BinaryUpdate& update, std::ofstream& log) {
  if (!path_is_regular_file(update.source)) {
    return true;
  }
  std::error_code ec;
  if (path_is_regular_file(update.target) &&
      file_contents_equal(update.source, update.target)) {
    log_line(log, "Binary already matches: " + update.target.string());
    return true;
  }

  set_update_status("Updating binaries",
                    "Replacing " + update.target.filename().string());
  auto backup = update.target;
  backup += ".bak";

  if (path_is_regular_file(update.target)) {
    std::filesystem::copy_file(update.target, backup,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
  }

  std::filesystem::copy_file(update.source, update.target,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  if (ec) {
    log_line(log, "Failed to copy " + update.source.string());
      if (path_is_regular_file(backup)) {
        std::filesystem::copy_file(backup, update.target,
                                   std::filesystem::copy_options::overwrite_existing,
                                   ec);
      }
    return false;
  }
  ::chmod(update.target.string().c_str(), 0755);
  return true;
}

std::vector<std::filesystem::path> find_deb_packages(
    const std::filesystem::path& base) {
  std::vector<std::filesystem::path> debs;
  std::error_code ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(base, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }
    if (to_lower(entry.path().extension().string()) == ".deb") {
      debs.push_back(entry.path());
    }
  }
  return debs;
}

struct StmFirmware {
  std::filesystem::path path;
  std::string kind;
};

std::vector<StmFirmware> find_stm_firmware(
    const std::filesystem::path& base) {
  std::vector<StmFirmware> firmware;
  std::error_code ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(base, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }
    const auto name = to_lower(entry.path().filename().string());
    const auto ext = to_lower(entry.path().extension().string());
    if (ext != ".bin" && ext != ".hex") {
      continue;
    }
    if (name.find("g4") != std::string::npos) {
      firmware.push_back({entry.path(), "g4"});
    } else if (name.find("c011") != std::string::npos) {
      firmware.push_back({entry.path(), "c011"});
    }
  }
  return firmware;
}

std::optional<std::string> read_port_from_json(
    const std::filesystem::path& path,
    const std::vector<std::string>& keys) {
  std::ifstream file(path);
  if (!file) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  const auto content = buffer.str();
  for (const auto& key : keys) {
    auto value = extract_string_field(content, key);
    if (value && !value->empty()) {
      return *value;
    }
  }
  return std::nullopt;
}

std::optional<std::string> find_serial_port_hint(const std::string& token) {
  std::error_code ec;
  const std::filesystem::path root("/dev/serial/by-id");
  if (!std::filesystem::exists(root, ec)) {
    return std::nullopt;
  }
  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (ec || !entry.is_symlink()) {
      continue;
    }
    const auto name = to_lower(entry.path().filename().string());
    if (name.find(token) != std::string::npos) {
      return entry.path().string();
    }
  }
  return std::nullopt;
}

std::optional<std::string> resolve_stm_port(
    const std::filesystem::path& base,
    const std::string& kind) {
  const std::filesystem::path local_config = base / "stm_ports.json";
  const std::filesystem::path global_config = "/Config/openhd/stm_ports.json";
  const std::vector<std::string> keys = {
      "stm_" + kind + "_port",
      kind + "_port"};

  if (auto value = read_port_from_json(local_config, keys)) {
    return value;
  }
  if (auto value = read_port_from_json(global_config, keys)) {
    return value;
  }

  if (kind == "g4") {
    if (auto hint = find_serial_port_hint("g4")) {
      return hint;
    }
  }
  if (kind == "c011") {
    if (auto hint = find_serial_port_hint("c011")) {
      return hint;
    }
  }
  return std::nullopt;
}

bool flash_stm_firmware(const StmFirmware& fw,
                        const std::filesystem::path& base,
                        std::ofstream& log) {
  if (!command_exists("stm32flash")) {
    log_line(log, "stm32flash not available for " + fw.path.string());
    set_update_status("Updating STM", "stm32flash not available", 1);
    return false;
  }
  const auto port = resolve_stm_port(base, fw.kind);
  if (!port) {
    log_line(log, "STM " + fw.kind + " port not configured");
    set_update_status("Updating STM", "Missing UART port for " + fw.kind, 1);
    return false;
  }
  set_update_status("Updating STM", "Flashing " + fw.kind + " over " + *port);
  const std::string cmd = "stm32flash -w '" + fw.path.string() +
                          "' -v -g 0x0 '" + *port + "' >> '" +
                          select_log_path() + "' 2>&1";
  if (!run_shell_command(cmd)) {
    log_line(log, "stm32flash failed for " + fw.path.string());
    return false;
  }
  return true;
}

bool apply_update_payload(const std::filesystem::path& base,
                          std::ofstream& log,
                          bool& reboot_required) {
  bool ok = true;
  bool changed = false;

  const std::vector<std::filesystem::path> apt_files = {
      base / "apt-packages.txt",
      base / "apt.txt",
      base / "apt_packages.txt"};

  std::vector<std::string> apt_packages;
  for (const auto& path : apt_files) {
    if (file_exists(path)) {
      auto list = read_package_list(path);
      apt_packages.insert(apt_packages.end(), list.begin(), list.end());
    }
  }
  if (!apt_packages.empty()) {
    if (!install_apt_packages(apt_packages, log)) {
      ok = false;
    } else {
      changed = true;
    }
  }

  const auto debs = find_deb_packages(base);
  if (!debs.empty()) {
    if (!apply_deb_updates(debs, log)) {
      ok = false;
    } else {
      changed = true;
    }
  }

  const auto binaries = find_binary_updates(base);
  for (const auto& item : binaries) {
    if (!apply_binary_update(item, log)) {
      ok = false;
      break;
    }
    changed = true;
  }

  const auto firmware = find_stm_firmware(base);
  for (const auto& fw : firmware) {
    if (!flash_stm_firmware(fw, base, log)) {
      ok = false;
      break;
    }
    changed = true;
  }

  reboot_required = changed;
  return ok;
}

void cleanup_update_source(const UpdateSource& source,
                           const std::optional<std::filesystem::path>& temp_dir) {
  std::error_code ec;
  if (!source.zip_path.empty()) {
    std::filesystem::remove(source.zip_path, ec);
  }
  if (!source.from_zip && !source.base_dir.empty()) {
    for (const auto& entry :
         std::filesystem::directory_iterator(source.base_dir, ec)) {
      if (ec) {
        break;
      }
      std::filesystem::remove_all(entry.path(), ec);
    }
  }
  if (temp_dir) {
    std::filesystem::remove_all(*temp_dir, ec);
  }
}

void run_update() {
  if (g_updating.exchange(true)) {
    return;
  }

  std::ofstream log(select_log_path(), std::ios::app);
  log_line(log, "----- OpenHD update started -----");
  set_update_status("Preparing update", "Update requested.");
  ensure_hold_file();
  stop_openhd_services();
  mask_openhd_services();

  bool reboot_required = false;
  bool success = true;

  auto source = find_update_source();
  if (!source) {
    set_update_status("No update", "No update payloads found.");
    log_line(log, "No update payloads found");
    g_updating = false;
    remove_hold_file();
    return;
  }

  std::optional<std::filesystem::path> temp_dir;
  std::filesystem::path base = source->base_dir;
  if (source->from_zip) {
    temp_dir = extract_zip(source->zip_path, log);
    if (!temp_dir) {
      set_update_status("Update failed", "Unable to extract update.zip", 2);
      log_line(log, "Failed to extract update.zip");
      g_updating = false;
      remove_hold_file();
      g_last_failure = std::chrono::steady_clock::now();
      return;
    }
    base = *temp_dir;
  }

  set_update_status("Applying update", "Processing update payloads.");
  success = apply_update_payload(base, log, reboot_required);

  if (success) {
    set_update_status("Update complete", "Update applied successfully.");
    log_line(log, "Update complete");
    cleanup_update_source(*source, temp_dir);
    unmask_openhd_services();
    if (reboot_required) {
      set_update_status("Reboot", "Rebooting after update.");
      log_line(log, "Rebooting after update");
      std::this_thread::sleep_for(std::chrono::milliseconds(800));
      (void)run_shell_command("reboot");
    }
  } else {
    set_update_status("Update failed", "Update did not complete.", 2);
    log_line(log, "Update failed");
    g_last_failure = std::chrono::steady_clock::now();
  }

  unmask_openhd_services();
  remove_hold_file();
  g_updating = false;
}

void update_worker() {
  while (true) {
    std::unique_lock<std::mutex> lock(g_update_mutex);
    g_update_cv.wait_for(lock, std::chrono::seconds(kUpdatePollSeconds));
    if (g_updating) {
      continue;
    }
    const bool requested = g_update_requested.exchange(false);
    const bool payload_ready = find_update_source().has_value();
    if (!requested && !payload_ready) {
      continue;
    }
    if (g_last_failure.time_since_epoch().count() != 0) {
      const auto since_fail =
          std::chrono::steady_clock::now() - g_last_failure;
      if (since_fail < std::chrono::seconds(kFailureBackoffSeconds)) {
        continue;
      }
    }
    lock.unlock();
    run_update();
  }
}

}  // namespace

void init_update_worker() {
  if (g_update_thread.joinable()) {
    return;
  }
  g_update_thread = std::thread(update_worker);
  g_update_thread.detach();
}

bool is_update_request(const std::string& line) {
  auto type = extract_string_field(line, "type");
  return type.has_value() && *type == "sysutil.update.request";
}

std::string handle_update_request(const std::string& line) {
  (void)line;
  g_update_requested = true;
  g_update_cv.notify_all();
  return "{\"type\":\"sysutil.update.response\",\"accepted\":true}\n";
}

bool is_updating() {
  return g_updating.load();
}

}  // namespace sysutil
