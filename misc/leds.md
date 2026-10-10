# Status LEDs

The controller keeps normal operation visibly lit. It renders the same meanings
on a single LED, a status/error pair, a separate-channel RGB LED, or Linux
multicolor LEDs. Animations run at 20 Hz in a separate SysUtils worker; writes
are skipped when values have not changed. No video or telemetry packet handling
is delayed for LED effects.

| State | Single LED | Status/error pair | RGB or multicolor pixels |
| --- | --- | --- | --- |
| Starting | Slow blink | Status slow blink | Blue breathing |
| Waiting | Double pulse every 2 seconds | Status double pulse | Amber breathing |
| Operating | Steady on | Status on, error off | Green glow with an activity pulse |
| Warning | Mostly on, two short interruptions | Status stays on, error double pulse | Amber over green |
| Fault | Three fast flashes, then a pause | Error flashes, status off | Red flashing |
| Updating | Fast blink | Alternating | Purple wave |
| Shutdown | On for 2 seconds, then off | Same | Fade out |

Air stays operating while transmitting without requiring an uplink. Ground
operates with incoming video or telemetry. Air video injection and Ground video
arrival enable the activity animation. This indicates transport activity, not
successful decoding or guaranteed RF delivery. Record-only mode uses cyan when
recording is active; it does not enable record-only mode in builds that disable it.
With multiple independently exposed multicolor LEDs, activity travels in opposite
directions for Air and Ground. One RGB LED uses a brightness pulse instead.

OpenHD samples existing link statistics on its control thread every 2 seconds.
Statistics older than 5 seconds are ignored. Runtime messages expire after 7
seconds, then the controller shows waiting. Old OpenHD versions without runtime
messages retain a steady ready indication. The wire message is:

```json
{"type":"indicator.runtime","source":"openhd","mode":"air","operating":true,"activity":true,"recording":false,"ttl_ms":7000}
```

Faults are retained per source until that source reports explicit recovery or
clear. Notifications and runtime activity cannot clear them. Shutdown and update
take priority over fault, then warning, then the base state. Temporary camera
setup and maintenance notifications expire back to the base state. Package
updates stay visible until the update worker reports completion or failure.
Diagnostic text still contributes to legacy error detection; RGB rendering no
longer classifies missing devices by sentence fragments.

## Board assignment

Default profiles select Rock 5A/5B/CM5 `user-led2`, Zero 3W `board-led`,
CM3 `pi-led-green`/`user-led`, and common `ACT`, `led0`, `work`, `work-led`,
`user-led` or `status` names. An OpenHD-named red/green/blue triplet is treated as
one RGB LED (including X20). OpenHD-named multicolor LEDs and WS281/SK681-named
multicolor LEDs are discovered in sorted order. PWR, unrelated network LEDs,
keyboard LEDs and storage indicators are left alone. CM3's power LED remains a
power indicator unless explicitly assigned.

For other boards, or an explicit pixel ordering, create `/etc/openhd/leds.conf`.
Names refer to entries in `/sys/class/leds`; paths are not accepted. A present
profile is authoritative; an empty profile disables LED ownership.

```text
# Ordinary status/error pair
primary ACT
secondary my-error-led
```

```text
# Separate-channel RGB: primary=green, secondary=red, blue=blue
primary openhd-x20dev:green:usr
secondary openhd-x20dev:red:usr
blue openhd-x20dev:blue:usr
```

```text
# Example independently exposed digital pixels, in physical order
pixel my-rgb-pixel0
pixel my-rgb-pixel1
pixel my-rgb-pixel2
```

Pixel entries require `multi_index` and `multi_intensity`, with red/green/blue
channels. Channel order, maximum brightness and optional per-channel maxima are
read from the driver. This follows the
[Linux multicolor LED interface](https://docs.kernel.org/leds/leds-class-multicolor.html).
Linux LED core handles electrical polarity; userspace does not invert it again.
Binary RGB channels support color changes; smooth breathing requires dimming.

X21 uses this backend if its image exposes the LEDs through the Linux LED class.
A proprietary character device, raw SPI strip or MCU interface needs its actual
driver protocol before a backend can be supplied. The currently available local
X21 sources do not establish that interface. No raw GPIO/SPI protocol is assumed.

## Device preview

Stop the running SysUtils service first, then run:

```sh
sudo openhd_sys_utils --led-preview
```

The command cycles the states and Air/Ground/record activity effects. It starts
no OpenHD services or first-boot tasks. An exclusive `/run/openhd/led.lock` prevents
two controllers fighting over LEDs. Ctrl+C restores the owned LEDs' previous
brightness, color and triggers. Normal SysUtils exit also restores them. Write
failures are logged rather than silently ignored.

Validation includes model, protocol and fake-sysfs tests. Actual board brightness,
pixel order, and appearance still need the preview on each device.
