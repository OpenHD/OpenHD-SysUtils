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

#include "sysutil_hostname.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unistd.h>

#include "sysutil_config.h"

namespace sysutil {
namespace {

std::optional<std::string> read_file_trimmed(const char* path) {
  std::ifstream file(path);
  if (!file) {
    return std::nullopt;
  }
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  const auto last = content.find_last_not_of(" \t\r\n");
  if (last == std::string::npos) {
    return std::nullopt;
  }
  content.erase(last + 1);
  const auto first = content.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return std::nullopt;
  }
  content.erase(0, first);
  if (content.empty()) {
    return std::nullopt;
  }
  return content;
}

std::string build_hostname(bool run_as_air) {
  std::string base = run_as_air ? "openhd_air" : "openhd_ground";
  const auto postfix = read_file_trimmed("/Config/name.txt");
  if (!postfix.has_value()) {
    return base;
  }
  return base + "_" + postfix.value();
}

void persist_hostname(const std::string& hostname) {
  std::ofstream file("/etc/hostname");
  if (!file) {
    std::cerr << "Failed to open /etc/hostname for writing" << std::endl;
    return;
  }
  file << hostname << "\n";
}

}  // namespace

void apply_hostname_if_enabled() {
  SysutilConfig config;
  const auto load_result = load_sysutil_config(config);
  if (load_result == ConfigLoadResult::Error) {
    return;
  }
  if (!config.set_hostname.value_or(false)) {
    return;
  }
  if (!config.run_mode.has_value()) {
    return;
  }
  const auto& run_mode = config.run_mode.value();
  if (run_mode != "air" && run_mode != "ground") {
    return;
  }
  const bool run_as_air = (run_mode == "air");
  const auto hostname = build_hostname(run_as_air);
  if (sethostname(hostname.c_str(), hostname.size()) != 0) {
    std::cerr << "Failed to set hostname: " << std::strerror(errno)
              << std::endl;
  }
  persist_hostname(hostname);
}

}  // namespace sysutil
