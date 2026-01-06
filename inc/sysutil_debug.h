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

#ifndef SYSUTIL_DEBUG_H
#define SYSUTIL_DEBUG_H

#include <string>

namespace sysutil {

// Initializes debug state by reading config and scanning debug.txt triggers.
void init_debug_info();
// Returns whether debug is enabled.
bool debug_enabled();
// Tests if the incoming message requests debug state.
bool is_debug_request(const std::string& line);
// Builds the debug response JSON payload.
std::string build_debug_response();

}  // namespace sysutil

#endif  // SYSUTIL_DEBUG_H
