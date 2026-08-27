#ifndef SYSUTIL_DISPLAY_H
#define SYSUTIL_DISPLAY_H

#include <string>

namespace sysutil {

// Applies the configured mode to the bootloader and OpenHD Glide. Returns true
// when either file changed and a reboot is required.
bool apply_display_config_if_needed();

bool is_display_request(const std::string& line);
bool is_display_update_request(const std::string& line);
std::string build_display_response();
std::string handle_display_update(const std::string& line);

}  // namespace sysutil

#endif  // SYSUTIL_DISPLAY_H
