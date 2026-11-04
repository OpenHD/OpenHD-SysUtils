#include "sysutil_pico_usb.h"
#include "sysutil_config.h"
#include "sysutil_devourer_usb.h"
#include <cstdlib>
#include <iostream>
#include <unistd.h>

bool sysutil::configure_pico_usb() {
    if (::access("/usr/sbin/openhd-pico-usb-role", X_OK) != 0) return true;
    SysutilConfig config;
    if (load_sysutil_config(config) == ConfigLoadResult::Error) return false;
    const bool ethernet = !config.disable_ethernet_link.value_or(true);
    if (ethernet) {
        config.nw_ethernet_card = "usb0";
        if (config.ground_unit_ip.value_or("").empty()) config.ground_unit_ip = "172.32.0.1";
        if (config.air_unit_ip.value_or("").empty()) config.air_unit_ip = "172.32.0.70";
        if (config.nw_manual_forwarding_ips.value_or("").empty())
            config.nw_manual_forwarding_ips = config.ground_unit_ip;
        if (!write_sysutil_config(config)) return false;
    }
    const auto result = std::system(ethernet ? "/usr/sbin/openhd-pico-usb-role device"
                                           : "/usr/sbin/openhd-pico-usb-role host");
    if (result != 0) {
        std::cerr << "[pico-usb] Failed to select " << (ethernet ? "USB Ethernet" : "radio host") << '\n';
        return false;
    }
    if (!ethernet) prepare_devourer_eu_usb();
    return true;
}
