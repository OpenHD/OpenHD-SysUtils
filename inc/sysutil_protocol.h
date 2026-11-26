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

#ifndef SYSUTIL_PROTOCOL_H
#define SYSUTIL_PROTOCOL_H

#include <optional>
#include <string>

namespace sysutil {

// Extracts a string field value from a JSON-like payload.
std::optional<std::string> extract_string_field(const std::string& line,
                                                const std::string& field);
// Extracts an integer field value from a JSON-like payload.
std::optional<int> extract_int_field(const std::string& line,
                                     const std::string& field);
// Extracts a boolean field value from a JSON-like payload.
std::optional<bool> extract_bool_field(const std::string& line,
                                       const std::string& field);
std::optional<std::string> extract_object_field(const std::string& line,
                                                const std::string& field);

}  // namespace sysutil

#endif  // SYSUTIL_PROTOCOL_H
