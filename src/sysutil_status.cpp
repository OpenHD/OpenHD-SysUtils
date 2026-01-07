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

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

#include "sysutil_protocol.h"

namespace sysutil {
namespace {

StatusSnapshot g_status;

std::uint64_t now_ms() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<milliseconds>(system_clock::now().time_since_epoch())
          .count());
}

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool contains_error_marker(const std::string& value) {
  if (value.empty()) {
    return false;
  }
  const auto lower = to_lower(value);
  return lower.find("error") != std::string::npos ||
         lower.find("fail") != std::string::npos ||
         lower.find("fatal") != std::string::npos ||
         lower.find("panic") != std::string::npos;
}

bool compute_has_error(const StatusSnapshot& status) {
  if (status.severity >= 2) {
    return true;
  }
  if (contains_error_marker(status.state) ||
      contains_error_marker(status.description) ||
      contains_error_marker(status.message)) {
    return true;
  }
  return false;
}

void update_status(const std::string& type,
                   const std::optional<std::string>& state,
                   const std::optional<std::string>& description,
                   const std::optional<std::string>& message,
                   const std::optional<int>& severity) {
  g_status.type = type;
  g_status.state = state.value_or("");
  g_status.description = description.value_or("");
  g_status.message = message.value_or("");
  g_status.severity = severity.value_or(0);
  g_status.updated_ms = now_ms();
  g_status.has_data = true;
  g_status.has_error = compute_has_error(g_status);
}

std::string json_escape(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

}  // namespace

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
    update_status(*type, state, description, message, severity);
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
    update_status(*type, state, description, message, severity);
    return;
  }

  if (type && *type == "indicator.clear") {
    g_status = StatusSnapshot{};
    g_status.type = *type;
    g_status.state = "CLEAR";
    g_status.description = "OpenHD status cleared.";
    g_status.updated_ms = now_ms();
    g_status.has_data = true;
    g_status.has_error = false;
    std::cout << "OpenHD state cleared." << std::endl;
    return;
  }

  if (state || description || message || severity) {
    if (type) {
      update_status(*type, state, description, message, severity);
    } else {
      update_status("status.update", state, description, message, severity);
    }
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

bool is_status_request(const std::string& line) {
  auto type = extract_string_field(line, "type");
  return type.has_value() && *type == "sysutil.status.request";
}

std::string build_status_response() {
  std::ostringstream out;
  out << "{\"type\":\"sysutil.status.response\",\"has_data\":"
      << (g_status.has_data ? "true" : "false")
      << ",\"has_error\":" << (g_status.has_error ? "true" : "false")
      << ",\"severity\":" << g_status.severity
      << ",\"updated_ms\":" << g_status.updated_ms
      << ",\"state\":\"" << json_escape(g_status.state)
      << "\",\"description\":\"" << json_escape(g_status.description)
      << "\",\"message\":\"" << json_escape(g_status.message) << "\"}\n";
  return out.str();
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
