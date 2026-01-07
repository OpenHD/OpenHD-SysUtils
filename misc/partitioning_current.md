# Current Partitioning Behavior (SysUtils C++)

This describes how partition resizing works today in the C++ sysutils code.

## Entry point and trigger
- `openhd_sys_utils` runs as root and calls `sysutil::resize_partition`
  from `run_firstboot_tasks`.
- The current implementation does not gate on resize flag files and does not
  perform any resize yet (see flow below).

Relevant code: `src/openhd_sys_utils.cpp`, `inc/sysutil_part.h`.

## Current flow (sysutil_part.cpp)
1) **Set status to partitioning**
   - Updates sysutils status snapshot with state `partitioning`.
2) **List partitions**
   - Runs `lsblk -P -o NAME,UUID,TYPE,MOUNTPOINT,FSTYPE,SIZE`.
   - Logs device, size, filesystem type, and mountpoint for each partition.
3) **Stop**
   - No resize, mkfs, or fstab changes yet.

Relevant code: `src/sysutil_part.cpp`.

## Notes / gaps vs legacy scripts
- The legacy `openhd_resize_util.sh` performed an actual resize + reboot; the
  current C++ flow only logs partitions.
- The legacy flow gated on `resize.txt`; the current flow runs unconditionally
  during firstboot tasks.
- Platform-specific resize flows from old init scripts (mkfs, fstab changes,
  external_video_part.txt, etc.) are not implemented in C++ yet.
