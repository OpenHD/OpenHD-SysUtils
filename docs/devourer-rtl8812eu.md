# RTL8812EU USB ownership

Current OpenHD builds use Devourer/libusb for RTL8812EU (RTL8822E). Loading the
kernel `88x2eu_ohd` driver first initializes the radio and exposes a netdev to
NetworkManager before OpenHD takes over. Live investigation observed repeated
kernel initialization, NetworkManager scans, USB disconnects and failed
enumeration during this startup gap. Removing this competing owner is a
prevention fix; it does not prove the cause of every USB error.

SysUtils requires `OPENHD_DEVOURER_RTL8812EU=ON` for every target, including X20:

- `/lib/modprobe.d/openhd-devourer-eu.conf` blocks alias and explicit loading of
  `88x2eu_ohd` across boots and replugs. The driver package remains installed.
- `/lib/udev/rules.d/70-openhd-devourer-eu.rules` keeps NetworkManager away from
  netdevs belonging to `0bda:a81a`, `0bda:a82a` and `0bda:e822`.
- Before discovery and service startup, SysUtils releases any matching legacy
  kernel interface left bound during an upgrade. It clears NetworkManager
  ownership first and skips active Devourer `usbfs` claims.

No hub/controller reset, USB port power cycle, EEPROM change, or driver module
unload is automated. An already failed USB port may still need a physical power
cycle once when upgrading. This policy cannot recover a non-enumerating device.

The legacy kernel-only build option is rejected at configuration time. ImageBuilder
also removes the AU/BU/CU/EU broadcast driver packages and bundled modules from
every image target, including X20. Onboard networking drivers remain necessary
for hotspot/client operation.

Run `ctest --output-on-failure` from the build directory for ownership tests. Exercise
both normal and X20 configurations and inspect the staged package files. Hardware
validation must separately establish a boot without a temporary `/run` blacklist,
RF video reception, and automatic channel following.

On 2026-10-05 the native ARMHF package passed its ownership test and runtime
checks on the Air Pi. A reboot with the temporary blacklist removed initialized
EU directly through Devourer; Ground received RF video and followed Air channel
changes. Later USB disconnects and OpenHD aborts were still observed with the
kernel driver absent. A controlled USB disconnect recovered without an abort.
This validates the ownership policy, not resolution of the remaining disconnect
and process-abort problem. A cold power-on and full image builds remain untested.
