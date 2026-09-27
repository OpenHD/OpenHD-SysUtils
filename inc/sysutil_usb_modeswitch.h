#ifndef SYSUTIL_USB_MODESWITCH_H
#define SYSUTIL_USB_MODESWITCH_H

namespace sysutil {

// Ejects Realtek 0bda:1a2b ZeroCD Wi-Fi adapters before Wi-Fi discovery.
// Returns true if a device was switched and re-enumerated.
bool switch_realtek_zerocd_wifi();

}  // namespace sysutil

#endif  // SYSUTIL_USB_MODESWITCH_H
