#pragma once
namespace sysutil {
// Apply ImageWriter's Ethernet selection before Wi-Fi detection/OpenHD startup.
// Other platforms, which do not install the Pico role helper, are untouched.
bool configure_pico_usb();
}
