#ifndef SYSUTIL_PROTOCOL_H
#define SYSUTIL_PROTOCOL_H

#include <optional>
#include <string>

namespace sysutil {

std::optional<std::string> extract_string_field(const std::string& line,
                                                const std::string& field);
std::optional<int> extract_int_field(const std::string& line,
                                     const std::string& field);

}  // namespace sysutil

#endif  // SYSUTIL_PROTOCOL_H
