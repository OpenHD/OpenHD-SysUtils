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

#ifndef SYSUTIL_STATUS_H
#define SYSUTIL_STATUS_H

#include <string>

namespace sysutil {

// Handles incoming status messages and logs important state.
void handle_status_message(const std::string& line);

// Tests if the given path points to an existing regular file.
bool is_regular_file(const std::string& path);

}  // namespace sysutil

#endif  // SYSUTIL_STATUS_H
