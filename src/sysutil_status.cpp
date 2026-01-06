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

#include "sysutil_status.h"

#include <iostream>
#include <sys/stat.h>

#include "sysutil_protocol.h"

namespace sysutil {

// Parses and logs status/indicator messages from OpenHD.
void handle_status_message(const std::string& line) {
  if (line.empty()) {
    return;
  }

  auto type = extract_string_field(line, "type");
  auto state = extract_string_field(line, "state");
  auto description = extract_string_field(line, "description");
  auto message = extract_string_field(line, "message");
  auto severity = extract_int_field(line, "severity");

  if (type && *type == "indicator.set") {
    std::string display;
    if (description) {
      display = *description;
    } else if (state && message) {
      display = *state + " (" + *message + ")";
    } else if (state) {
      display = *state;
    } else if (message) {
      display = *message;
    } else {
      display = "UNKNOWN";
    }
    std::cout << "OpenHD state: " << display << std::endl;
    return;
  }

  if (type && *type == "indicator.status") {
    return;
  }

  if (type && *type == "indicator.clear") {
    std::cout << "OpenHD state cleared." << std::endl;
    return;
  }

  if (state || description || message || severity) {
    std::string display;
    if (description) {
      display = *description;
    } else if (state && message) {
      display = *state + " (" + *message + ")";
    } else if (state) {
      display = *state;
    } else if (message) {
      display = *message;
    }
    if (!display.empty()) {
      std::cout << "OpenHD state: " << display << std::endl;
    } else {
      std::cout << "OpenHD state update received." << std::endl;
    }
    return;
  }

  std::cout << "OpenHD message: " << line << std::endl;
}

// Returns true when the path exists and points to a regular file.
bool is_regular_file(const std::string& path) {
  struct stat st {};
  if (stat(path.c_str(), &st) != 0) {
    return false;
  }
  return S_ISREG(st.st_mode);
}

}  // namespace sysutil
