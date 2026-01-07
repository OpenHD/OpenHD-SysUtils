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

#ifndef SYSUTIL_CONFIG_H
#define SYSUTIL_CONFIG_H

#include <optional>
#include <string>

namespace sysutil {

struct SysutilConfig {
  // Cached platform type id (if known).
  std::optional<int> platform_type;
  // Cached platform name (if known).
  std::optional<std::string> platform_name;
  // Persisted debug flag.
  std::optional<bool> debug_enabled;
  // Pending OpenHD reset request.
  std::optional<bool> reset_requested;
  // Requested boot mode ("air" or "ground").
  std::optional<std::string> run_mode;
  // First-boot gate for one-time detection tasks.
  std::optional<bool> firstboot;
  // Detected init system (e.g. systemd or init.d).
  std::optional<std::string> init_system;
  // Detected shell type (e.g. busybox or bash).
  std::optional<std::string> shell;
};

// Result of attempting to load the config file.
enum class ConfigLoadResult {
  NotFound,
  Loaded,
  Error,
};

// Returns the on-disk sysutils config path.
const char* sysutil_config_path();
// Loads config values from disk into the provided struct.
ConfigLoadResult load_sysutil_config(SysutilConfig& config);
// Writes config values only if the config file does not yet exist.
bool write_sysutil_config_if_missing(const SysutilConfig& config);
// Writes config values, overwriting any existing file.
bool write_sysutil_config(const SysutilConfig& config);
// Removes the config file if it exists.
bool remove_sysutil_config();

}  // namespace sysutil

#endif  // SYSUTIL_CONFIG_H
