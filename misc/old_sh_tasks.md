# Old Shell Tasks vs C++ SysUtils

Legend:
- Done: implemented in C++ sysutils
- Partial: some behavior covered, gaps remain
- Needed: not present in C++ sysutils

## Might still be needed, but propably not 
- Migrate x86 image layout by moving /boot/openhd/openhd/* into /boot/openhd (old/openhd_sys_utils.sh). Status: Needed.

## Boot and platform init orchestration
- Trigger platform-specific init scripts based on flag files (X20, X86, Rock 5, RK3566, RPi) (old/openhd_sys_utils.sh + old/init*.sh). Status: Needed.
- Enable SSH on Rock 5 boards during init (old/initRock.sh). Status: Needed.

## Platform detection and manifest
- Create platform marker directories/files under /usr/local/share/openhd_platform or /usr/local/share/openhd/platform (old/initX20.sh, old/initRock.sh). Status: Needed.

## Partition and storage management
- Platform-specific recordings partition flow with mkfs, mount, fstab updates, and external_video_part.txt marker. Status: Done for generic FAT32 recordings partitions.
- x86 example service for resizing partitions with a stored UUID (old/x86-core.sh). Status: Needed.

## Camera setup and configuration
- Camera type selection and overlay config for RPi and Rockchip platforms (old/ohd_camera_setup.sh). Status: Needed.
- Rock 5 camera extlinux.conf selection by IMX* file (old/initRock.sh). Status: Needed.
- X20 HDZero module init and wifi card type markers (old/initX20.sh). Status: Needed.
- Runcam V1/V2/V3/Nano90 detection and I2C control (resolution, WB, contrast, flip, etc.) (old/x20/runcam_v2/*). Status: Needed.
- Custom unmanaged camera pipelines and network setup (old/custom_unmanaged_camera.sh). Status: Needed.

## Video decode helpers
- H264/H265/MJPEG decode pipelines and fpvue helpers (old/h264_decode.sh, old/h265_decode.sh, old/mjpeg_decode.sh). Status: Needed.

## Status and indicator handling
- Poll /tmp/openhd_status/*/warning and wall messages (old/status.sh). Status: Needed.
- Socket-based indicator/status logging (C++: src/openhd_sys_utils.cpp + src/sysutil_status.cpp). Status: Done.

## Update and install helpers
- Apply update.zip (deb install, logging, reboot/fail wall) (old/openhd_update_utils.sh). Status: Needed.
- Package install hooks for motd, autologin, and cleanup (old/packaging/before-install.sh, old/packaging/after-install.sh). Status: Needed.

## LED control
- Cross-platform LED status patterns and platform LED selection. Status: Done for RPi, X20, Rock 5A/5B, CM5, CM3, and Zero3-style LEDs.

## EMMC utilities
- Board detection and EMMC clear/flash with LED feedback (old/openhd_emmc_util.sh). Status: Needed.

## Desktop and Steam Deck tweaks
- Trust all Desktop .desktop files and restart Nautilus (old/desktop-truster.sh). Status: Needed.
- Steam Deck screen rotation and touch matrix fix (old/steamdeck.sh, old/initX86.sh). Status: Needed.
