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

#ifndef SYSUTIL_UPDATE_H
#define SYSUTIL_UPDATE_H

#include <string>

namespace sysutil {

// Starts the background update worker.
void init_update_worker();

// Checks whether a message requests an update run.
bool is_update_request(const std::string& line);

// Handles an update request and returns a response payload.
std::string handle_update_request(const std::string& line);

// Returns true while an update is running.
bool is_updating();

}  // namespace sysutil

#endif  // SYSUTIL_UPDATE_H
