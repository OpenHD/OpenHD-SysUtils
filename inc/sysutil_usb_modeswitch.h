#ifndef SYSUTIL_USB_MODESWITCH_H
#define SYSUTIL_USB_MODESWITCH_H

namespace sysutil {

// Ejects Realtek 0bda:1a2b ZeroCD Wi-Fi adapters before Wi-Fi discovery.
// Returns true if a device was switched and re-enumerated.
// Power recovery is allowed only after discovery confirms no working link card.
bool switch_realtek_zerocd_wifi(bool allow_power_recovery = false);

}  // namespace sysutil

#endif  // SYSUTIL_USB_MODESWITCH_H
