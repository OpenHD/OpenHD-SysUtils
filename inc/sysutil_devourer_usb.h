#ifndef SYSUTIL_DEVOURER_USB_H
#define SYSUTIL_DEVOURER_USB_H

#include <filesystem>
#include <functional>
#include <string>

namespace sysutil {

// Release an EU kernel interface left bound during a package upgrade. The
// packaged modprobe policy prevents subsequent binding at boot and replug.
// A failed NetworkManager notification leaves that interface bound for safety.
// No hub reset, USB authorization change, or driver module unload is performed.
unsigned prepare_devourer_eu_usb(
    const std::filesystem::path& usb_root = "/sys/bus/usb/devices",
    const std::function<bool(const std::string&)>& set_unmanaged = {});

}  // namespace sysutil
#endif
