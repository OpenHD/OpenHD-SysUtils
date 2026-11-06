#pragma once
#include "sysutil_config.h"

namespace sysutil {
// Missing ImageWriter selections must leave a usable camera/radio air unit.
// Explicit settings, including USB Ethernet, remain authoritative.
inline bool apply_pico_defaults(SysutilConfig& config) {
    bool changed = false;
    if (!config.run_mode || (config.run_mode != "air" && config.run_mode != "ground")) {
        config.run_mode = "air";
        changed = true;
    }
    if (!config.disable_ethernet_link) {
        config.disable_ethernet_link = true;
        changed = true;
    }
    if (!config.wifi_enable_autodetect) {
        config.wifi_enable_autodetect = true;
        changed = true;
    }
    return changed;
}
}
