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

#ifndef SYSUTIL_PLATFORM_H
#define SYSUTIL_PLATFORM_H

#include <string>

namespace sysutil {

struct PlatformInfo {
  // Numeric platform id.
  int platform_type = 0;
  // Human-readable platform name.
  std::string platform_name;
};

// Performs full platform discovery without caching.
PlatformInfo discover_platform_info();
// Initializes cached platform info (loading config or detecting when needed).
void init_platform_info();
// Returns the cached platform info, initializing on first use.
const PlatformInfo& platform_info();
// Tests if the incoming message is a platform request.
bool is_platform_request(const std::string& line);
// Builds the platform response JSON payload.
std::string build_platform_response();

}  // namespace sysutil

#endif  // SYSUTIL_PLATFORM_H
