# ORQA DTK runtime support

ORQA OS is Yocto based. Installing the SysUtils executable alone does not
install its Debian package dependencies. In particular, Realtek ZeroCD adapters
(`0bda:1a2b`) need `/usr/sbin/usb_modeswitch`. SysUtils invokes the standard
eject sequence and waits for USB re-enumeration before radio discovery. The
30-second udev grace applies when the distribution's modeswitch dispatcher is
installed; standalone installations switch immediately during startup.

Source the ORQA SDK environment and build the executable with:

```sh
bash OpenHD-SysUtils/scripts/build_usb_modeswitch.sh build-orqa/usb_modeswitch
```

The build downloads USB_ModeSwitch 2.6.2 from its upstream release site, verifies
its SHA256, links against the SDK's libusb, and preserves its license. The ORQA
CI deploy archive includes it under `usr/sbin`, together with the camera probe.
The image assembler copies that tree into the root filesystem. USB_ModeSwitch's
Tcl dispatcher is unnecessary for SysUtils' direct executable invocation.

## Camera signal detection

The tested SDK registers two `orqa,dummy-csi2-sensor` platform devices, without
camera I2C identities. Its frame-size range of 16 through 16383 and its
30/60/120 fps enumeration describe the dummy driver, not camera capabilities.
Even an invalid capture size may dequeue buffers containing repeated sections
and unwritten rows. Do not select a camera based on device nodes or enumeration
alone.

The optional Python 3 diagnostic performs bounded captures with OpenHD stopped,
rejects zero-filled trailing image rows, and measures frame arrival rate. It
restores the original media formats and does not change camera selections:

```sh
systemctl stop openhd
trap 'systemctl start openhd' EXIT
/usr/local/sbin/openhd-orqa-camera-probe
```

On a fresh first boot, SysUtils invokes the probe with a 20-second limit and
fills missing camera selections and resolution defaults from matched signals.
Explicit selections (including a disabled secondary camera) are preserved.
If the optional probe cannot run, the existing Rekindle fallback remains.
This provisioning requires the matching OpenHD build with ORQA v1 ID 126 and
secondary ORQA camera support, plus Python 3 and v4l2-ctl on the image.

It compares the current signals with two known profiles; this is not a model-ID
reader or an exhaustive enumeration of programmable camera modes. Confirm raw
image geometry when bringing up a different prototype. I2C addresses alone are
not sufficient to name an unknown camera, and a receive-byte scan may miss
devices requiring a register-address transaction.

## Board validation on 2026-10-19

On `192.168.1.75` (ORQA DTK Standalone Plus, ORQA OS 6.6-scarthgap):

| Path | Native capture | Measured signal | Profile |
| --- | --- | --- | --- |
| CSI0, sensor subdev3, receiver subdev0 | `/dev/video2` | 1920x1080 at 60 fps | ORQA v1, ID 126 |
| CSI1, sensor subdev2, receiver subdev1 | `/dev/video3` | 960x720 at 120 fps | RunCam Wasp, legacy Hornet ID 122 |

Raw images were inspected. The Wasp's larger-size captures contained repeated
sections and padding; the prototype's 1920x1440 capture had padding below line
1080. These are observed current output modes, not a claim that the prototype
cannot support other modes through its own firmware/control interface.

Unregistered receive-byte responders were observed at I2C bus 1 addresses
`0x55`/`0x72` and bus 3 address `0x23`; their relationship to the cameras remains
unknown. No register writes or forced kernel-driver takeovers were used.

SysUtils switched the radio to `0bda:c812`, identified its Devourer RTL8822C
backend, and OpenHD initialized it. This does not establish RF reception at a
ground station. Board backups are in `/root/openhd-backup-20261019-sysutils`.

The board was rebooted after deployment: both services started, the radio was
still in Wi-Fi mode, and both Hantro capture/encoder contexts started with the
configured dimensions. A passive TCP/MAVLink sample reported about 15 Mbit/s
software TX with zero failed packet injections, but slow-injection hints and
video frame-queue drops were observed at the existing 11 Mbit/s aggregate video
target. No RF frequency, power, MCS, or bitrate-budget settings were changed.
Raw capture measured 60/120 fps; concurrent encoding had some startup capture
drops, so this is not a loss-free end-to-end dual-stream claim.

A separate manual service stop also exposed a telemetry shutdown abort
(`pure virtual method called`) with a connected MAVLink TCP client. The unit
started again; that existing shutdown path was not changed by this work.
The active signal probe and its flat provisioning output were tested live;
fresh-image first-boot provisioning and full CI image assembly were not rerun.

### FC UART investigation (2026-10-19)

The active FC port was `/dev/ttymxc0`, 57600 baud, 8N1, without flow control.
It remained open; the service log contained no repeated UART reopen events.
The Linux console and getty used `/dev/ttymxc1`, not the FC port.

Passive 30-second MAVLink samples received only two FC heartbeats at the air
unit and three at ground, with a 25.968-second gap at ground. Other FC messages
continued arriving. QOpenHD's four-second heartbeat timeout therefore explains
the apparent repeated FC disconnects, but not the underlying packet loss.

With OpenHD stopped, a separate 20-second raw serial capture contained 3876
bytes. Independent pymavlink parsing found 19 pressure messages, 19 status
texts, 18 battery messages, and only one heartbeat. Repeated truncated headers
(`fd 1f fd 1f`) occurred ahead of otherwise valid packets. UART counters reported
zero framing, parity, overrun, break, and receive-buffer-overrun errors. Forcing
runtime power management on did not improve the capture; its original `auto`
setting was restored. These observations locate the corruption before OpenHD
forwarding, but do not establish whether the FC transmitter or ORQA receive/DMA
path is responsible. FC transmit-buffer overflow is a hypothesis, not a verified
diagnosis. OpenHD was restarted after each capture.

Received FC heartbeats were system/component 1/1, MAV_AUTOPILOT_GENERIC (0),
vehicle type 2. These fields do not uniquely identify INAV or its version.
The user identifies the firmware as INAV 7.x or 8.x. No AUTOPILOT_VERSION was
observed. Two rounds of read-only MSP API/variant/version queries at the active
baud rate produced no MSP replies. No FC settings were written.

Upstream INAV 8.0.1's MAVLink receiver does not implement PARAM_SET,
AUTOPILOT_VERSION requests, SERIAL_CONTROL, or MSP tunnelling. Its parameter-list
handler explicitly reports zero parameters. Therefore a QOpenHD toggle for
`dshot_beeper_enabled` needs an available MSP configuration connection or a
corresponding FC firmware extension; the present telemetry-only connection has
not demonstrated that capability. Exact board/version/configuration inspection
needs FC configuration access (for example temporary USB). Do not infer a
version from heartbeat IDs or present an unconfirmed beeper write as successful.

References:
- https://github.com/iNavFlight/inav/blob/8.0.1/src/main/telemetry/mavlink.c
- https://github.com/iNavFlight/inav/blob/8.0.1/src/main/msp/msp_protocol.h
