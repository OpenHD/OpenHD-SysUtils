#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sysutil {
struct UsbHubPowerInfo {
  unsigned ports = 0;
  bool individual = false;
  bool switchable = false;
};
struct UsbHubPowerTarget {
  std::filesystem::path hub;
  std::vector<unsigned> ports;
};
using UsbHubQuery = std::function<std::optional<UsbHubPowerInfo>(
    const std::filesystem::path &)>;
// Empty means unsupported topology or a USB storage device would lose power.
std::vector<UsbHubPowerTarget>
plan_usb_wifi_power_cycle(const std::filesystem::path &usb_root,
                          const std::string &device_port, bool pi4_shared_power,
                          const UsbHubQuery &query);
// Always attempts to restore every port for which power-off was requested,
// including a failed/ambiguous power-off request.
bool cycle_usb_power(const std::vector<UsbHubPowerTarget> &targets,
                     const std::function<bool(const std::filesystem::path &,
                                              unsigned, bool)> &set_power,
                     const std::function<void()> &wait_off);
bool recover_zerocd_usb_power(const std::string &device_port);
// Entry point for the independently supervised recovery worker.
bool perform_zerocd_usb_power_recovery(const std::string &device_port,
                                       bool check_only = false);
} // namespace sysutil
