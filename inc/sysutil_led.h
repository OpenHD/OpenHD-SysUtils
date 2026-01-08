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

#ifndef SYSUTIL_LED_H
#define SYSUTIL_LED_H

#include "sysutil_status.h"

namespace sysutil {

// Discover available LEDs and start the LED worker.
void init_leds();
// Update LED pattern from the latest status.
void update_leds_from_status(const StatusSnapshot& status);

}  // namespace sysutil

#endif  // SYSUTIL_LED_H
