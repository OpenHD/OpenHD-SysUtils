#include "sysutil_protocol.h"

#include <cctype>

namespace sysutil {
namespace {

std::size_t find_field_key(const std::string& line, const std::string& field) {
  const std::string needle = "\"" + field + "\"";
  return line.find(needle);
}

std::size_t skip_ws(const std::string& line, std::size_t pos) {
  while (pos < line.size() &&
         std::isspace(static_cast<unsigned char>(line[pos]))) {
    ++pos;
  }
  return pos;
}

}  // namespace

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

}  // namespace sysutil
