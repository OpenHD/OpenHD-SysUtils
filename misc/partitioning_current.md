# Current Partitioning Behavior (SysUtils C++)

This describes how partition resizing works today in the C++ sysutils code.

## Entry point and trigger
- `openhd_sys_utils` runs as root and calls `sysutil::resize_partition_firstboot`
  from `run_firstboot_tasks`.
- The first-boot implementation finds a resizable FAT32/unformatted partition
  with at least 1 GiB free space after it, formats it as the `RECORDINGS`
  partition, expands it to fill the remaining device, mounts it at `/Video`,
  and writes the legacy `external_video_part.txt` marker.

Relevant code: `src/openhd_sys_utils.cpp`, `inc/sysutil_part.h`.

## Current flow (sysutil_part.cpp)
1) **Set status to partitioning**
   - Updates sysutils status snapshot with state `partitioning`.
2) **Detect partition layout**
   - Runs `lsblk` and reads partition size, start offset, parent disk,
     filesystem type, label, and mountpoint.
3) **Resize recordings partition**
   - Formats the candidate as FAT32, expands it with `parted resizepart`,
     relabels it `RECORDINGS`, sets FAT32 LBA type, updates `/etc/fstab`,
     mounts it at `/Video`, and writes `/Video/external_video_part.txt`.
4) **Mount known partitions**
   - Mounts `RECORDINGS` at `/Video` and `OPENHD` at `/Config`.
   - Exposes legacy `/config` and `/conf` aliases when `/Config` is mounted.

Relevant code: `src/sysutil_part.cpp`.

## Notes / gaps vs legacy scripts
- The generic recordings partition flow is implemented in C++.
- The legacy root filesystem resize helper by UUID still exists as
  `resize_partition_by_uuid`, but the normal first-boot flow now handles the
  recordings partition directly.
