# Realtek ZeroCD recovery

Realtek CU-family radios can enumerate as the virtual driver disk `0bda:1a2b`
instead of a radio such as `0bda:c812`. Do not infer the chipset from the
virtual disk ID: several Realtek families share it.

SysUtils first gives the distribution's modeswitch dispatcher 30 seconds,
then makes up to three bounded standard-eject attempts. When radio discovery
still finds no usable OpenHD link card, it can power-cycle the stuck adapter:

- Individual-power hubs: switch only the adapter's port.
- Ganged-power hubs: switch all ports required to remove power.
- Pi 4 internal VIA hub: switch its USB2 ports and the USB3 companion root
  ports together; the advertised per-port switches share the physical rail.
- Refuse a plan that would power off another USB storage device, including
  USB boot disks. Skip hubs that do not advertise power switching.

The recovery worker uses Linux USB hub class requests directly; `uhubctl`
and a board-specific GPIO utility are not required. Board-specific hubs can
only recover electrically when their hardware implements the advertised
power switch. Other USB devices on a shared rail, including Ethernet, may
briefly disconnect. Power is restored locally, without a network connection.

`openhd-usb-power-recover` is installed alongside SysUtils. On systemd images
it runs in an independent transient service, so restarting SysUtils does not
kill the worker during the power-off interval. Non-systemd images run a
separate session. The helper completes power restoration on normal termination
requests, retries failed power-on requests, and logs hardware failures.

Only one power-cycle attempt per physical port is permitted per boot,
recorded under `/run/openhd/usb-power-recovery`. Re-enumeration changes USB
device numbers but must not reset this budget. Healthy adapters are never
cycled by this fallback. Native CU mode alone is not treated as a failure.

Debian packages install the helper through CMake. ORQA deploy bundles, Pi 5
native images, and X21/Luckfox Aura/Pico component archives explicitly include it.
Standalone installations must copy the helper as well as `openhd_sys_utils`.

To inspect the real hub descriptors and proposed ports without changing power:

```sh
sudo /usr/local/sbin/openhd-usb-power-recover --check 1-1.3
```

Use the adapter's physical sysfs port from the target board. Check mode also
works on an already functioning Realtek radio and never consumes the reset budget.

The simulated-topology tests cover isolated ports, ganged hubs, Pi 4 companion
rails, storage protection, invalid ports, and restoration after failure. The
shared Pi 4 rail cycle and Pi 5 dual-video display were tested on hardware;
other board families require their own hardware validation.
