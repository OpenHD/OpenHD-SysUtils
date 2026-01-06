#ifndef SYSUTIL_STATUS_H
#define SYSUTIL_STATUS_H

#include <string>

namespace sysutil {

void handle_status_message(const std::string& line);

// Tests if the given path points to an existing regular file.
bool is_regular_file(const std::string& path);

}  // namespace sysutil

#endif  // SYSUTIL_STATUS_H
