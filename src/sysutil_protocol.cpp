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

#include "sysutil_protocol.h"

#include <cctype>

namespace sysutil {
namespace {

// Finds the JSON key position for a field name.
std::size_t find_field_key(const std::string& line, const std::string& field) {
  const std::string needle = "\"" + field + "\"";
  return line.find(needle);
}

// Skips whitespace from a given position.
std::size_t skip_ws(const std::string& line, std::size_t pos) {
  while (pos < line.size() &&
         std::isspace(static_cast<unsigned char>(line[pos]))) {
    ++pos;
  }
  return pos;
}

}  // namespace

// Extracts a quoted string field.
std::optional<std::string> extract_string_field(const std::string& line,
                                                const std::string& field) {
  std::size_t key_pos = find_field_key(line, field);
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }
  std::size_t pos = line.find(':', key_pos);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  pos = skip_ws(line, pos + 1);
  if (pos >= line.size() || line[pos] != '"') {
    return std::nullopt;
  }
  ++pos;
  std::string value;
  value.reserve(16);
  bool escape = false;
  for (; pos < line.size(); ++pos) {
    char ch = line[pos];
    if (escape) {
      switch (ch) {
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        case '\\':
        case '"':
        default:
          value.push_back(ch);
          break;
      }
      escape = false;
      continue;
    }
    if (ch == '\\') {
      escape = true;
      continue;
    }
    if (ch == '"') {
      return value;
    }
    value.push_back(ch);
  }
  return std::nullopt;
}

// Extracts an integer field.
std::optional<int> extract_int_field(const std::string& line,
                                     const std::string& field) {
  std::size_t key_pos = find_field_key(line, field);
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }
  std::size_t pos = line.find(':', key_pos);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  pos = skip_ws(line, pos + 1);
  if (pos >= line.size()) {
    return std::nullopt;
  }

  bool neg = false;
  if (line[pos] == '-') {
    neg = true;
    ++pos;
  }
  if (pos >= line.size() || !std::isdigit(static_cast<unsigned char>(line[pos]))) {
    return std::nullopt;
  }

  int value = 0;
  for (; pos < line.size(); ++pos) {
    char ch = line[pos];
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      break;
    }
    value = value * 10 + (ch - '0');
  }
  return neg ? -value : value;
}

// Extracts a boolean field, accepting true/false or 0/1.
std::optional<bool> extract_bool_field(const std::string& line,
                                       const std::string& field) {
  std::size_t key_pos = find_field_key(line, field);
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }
  std::size_t pos = line.find(':', key_pos);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  pos = skip_ws(line, pos + 1);
  if (pos >= line.size()) {
    return std::nullopt;
  }
  if (line.compare(pos, 4, "true") == 0) {
    return true;
  }
  if (line.compare(pos, 5, "false") == 0) {
    return false;
  }
  if (line[pos] == '0') {
    return false;
  }
  if (line[pos] == '1') {
    return true;
  }
  return std::nullopt;
}

}  // namespace sysutil
