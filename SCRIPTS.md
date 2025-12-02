# Script Overview

This repository contains helper scripts used by OpenHD system images. Use this document to quickly identify what each script does and where it is expected to run.

## Top-level utilities

- `custom_unmanaged_camera.sh`: Optional hook for custom camera integrations. When `/boot/openhd/unmanaged.txt` exists, it can stand up network connections and stream pipelines for IP or thermal cameras into OpenHD via UDP ports 5500/5501. User-defined streaming functions are provided but commented out by default.
- `desktop-truster.sh`: Marks all `.desktop` launchers on the current user's desktop as trusted using `gio`, reducing prompts when launching shortcuts.
- `h264_decode.sh` / `h265_decode.sh`: Sample decode pipelines for receiving OpenHD video over RTP on port 5600 and playing back via GStreamer. The scripts select pipeline variants based on connected hardware (e.g., Rockchip boards) and include helpers for direct framebuffer output.
- `initPi.sh`: Performs Raspberry Pi specific boot-time setup. Detects kernel version/board, installs overlays, and toggles additional modules and services according to available hardware markers.
- `initRock.sh`: Adds Rockchip platform identification files and ensures the OpenHD systemd services are enabled when Rockchip markers are present.
- `initX20.sh`: Initializes the Walksnail Avatar / Caddx Vista (X20) air unit by writing platform markers and enabling temperature monitoring services.
- `initX86.sh`: Applies x86-specific tweaks, including handling Steam Deck display rotation and enabling the main `openhd_sys_utils` service.
- `led.sh` / `led_sys.sh`: Simple LED control helpers that write colored status combinations to `/usr/local/share/openhd/led`. The `_sys` variant reads its inputs from files created by the main system services.
- `mjpeg_decode.sh`: A misnamed but functional H265 decode helper mirroring the pipelines in the other decode scripts for Rockchip hardware.
- `ohd_camera_setup.sh`: Detects the current platform and writes appropriate boot configuration (e.g., enabling CSI cameras on Raspberry Pi, Rockchip camera setup, or assigning fallback values) based on `uname`/`lsb_release` data.
- `openhd_emmc_util.sh`: Utility for clearing or burning images to embedded MMC storage. Supports commands such as `clear` and `burn`, optionally rebooting after completion.
- `openhd_resize_util.sh`: Resizes the configured filesystem when a `resize.txt` sentinel file is present under `/boot/openhd`. Uses `fdisk`, `partprobe`, and `resize2fs` to grow the partition before rebooting.
- `openhd_sys_utils.sh`: Primary orchestration script executed at boot. Handles debug mode, triggers platform-specific init scripts, optionally clears or resizes storage, and runs camera setup helpers. It also responds to special marker files to perform maintenance tasks.
- `openhd_update_utils.sh`: Installs `.deb` packages placed in `/boot/openhd/update`. Processes optional `update.zip`, logs results to `/boot/openhd/install-log.txt`, and reboots on success.
- `status.sh`: Aggregates status files under `/tmp/openhd_status` into a single `status.txt`, checking for warning/error markers to set the exit code.
- `steamdeck.sh`: Convenience script to rotate the display on Steam Deck hardware when the touchscreen device is detected.
- `x86-core.sh`: Core service entrypoint for generic x86 images. Handles first-boot filesystem resize, optional Steam Deck rotation, enabling VNC, and configuring wireless access points.

## Packaging scripts

- `packaging/before-install.sh`: Cleans up MOTD and leftover `x20` header artifacts before packaging installs.
- `packaging/after-install.sh`: Restores the MOTD, configures Raspberry Pi autologin when applicable, and prunes X20-specific files on non-custom installations.

## Other resources

- `audio/audio_playback_rpi.service`: Systemd unit to launch Raspberry Pi audio playback for OpenHD.
- `misc/`: Collection of configuration assets (boot configuration templates, MOTD, wallpaper, and user prompts) used during packaging or platform setup.
- `shortcuts/`: Desktop launchers and icons for OpenHD utilities and companion applications.
- `x20/`: Platform-specific resources for the X20 hardware family, including temperature guardian service assets and binary blobs.
