#include "sysutil_pico_defaults.h"
#include <cassert>

int main() {
    sysutil::SysutilConfig config;
    assert(sysutil::apply_pico_defaults(config));
    assert(config.run_mode == "air");
    assert(config.disable_ethernet_link == true);
    assert(config.wifi_enable_autodetect == true);
    assert(!sysutil::apply_pico_defaults(config));
    // A deliberate Ethernet or ground selection must still work.
    config.run_mode = "ground";
    config.disable_ethernet_link = false;
    config.wifi_enable_autodetect = false;
    assert(!sysutil::apply_pico_defaults(config));
    assert(config.run_mode == "ground");
    assert(config.disable_ethernet_link == false);
    assert(config.wifi_enable_autodetect == false);
    config.run_mode = "invalid";
    assert(sysutil::apply_pico_defaults(config));
    assert(config.run_mode == "air");
}
