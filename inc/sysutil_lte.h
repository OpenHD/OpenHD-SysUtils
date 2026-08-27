#ifndef SYSUTIL_LTE_H
#define SYSUTIL_LTE_H

#include <string>

namespace sysutil {

struct LteProfile {
  bool configured = false;
  bool active = false;
  std::string device_id;
  std::string fleetcontrol_address;
  std::string interface_name;
  int video_port = 0;
  int video2_port = 0;
  int telemetry_port = 0;
};

// Loads the non-secret OpenHD metadata embedded as comments in a wg-quick
// profile. The WireGuard private key never leaves this process.
LteProfile load_lte_profile(const std::string& path);

// Activates the configured wg-quick profile once at SysUtils startup.
LteProfile initialize_lte_link();

// Returns the last known LTE profile/status for the settings socket.
LteProfile lte_profile_status();

}  // namespace sysutil

#endif  // SYSUTIL_LTE_H
