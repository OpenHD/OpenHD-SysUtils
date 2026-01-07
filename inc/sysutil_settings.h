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

#ifndef SYSUTIL_SETTINGS_H
#define SYSUTIL_SETTINGS_H

#include <string>

namespace sysutil {

// Consumes boot-time marker files and persists them in sysutils config.
void sync_settings_from_files();

// True when the payload requests sysutils settings.
bool is_settings_request(const std::string& line);
// True when the payload updates sysutils settings.
bool is_settings_update(const std::string& line);
// Builds the settings response payload.
std::string build_settings_response();
// Applies a settings update and returns a response payload.
std::string handle_settings_update(const std::string& line);

}  // namespace sysutil

#endif  // SYSUTIL_SETTINGS_H
