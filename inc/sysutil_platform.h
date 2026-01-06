#ifndef SYSUTIL_PLATFORM_H
#define SYSUTIL_PLATFORM_H

#include <string>

namespace sysutil {

struct PlatformInfo {
  int platform_type = 0;
  std::string platform_name;
};

void init_platform_info();
const PlatformInfo& platform_info();
bool is_platform_request(const std::string& line);
std::string build_platform_response();

}  // namespace sysutil

#endif  // SYSUTIL_PLATFORM_H
