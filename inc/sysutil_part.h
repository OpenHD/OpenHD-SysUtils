/******************************************************************************
 * OpenHD
 *
 * Licensed under the GNU General Public License (GPL) Version 3.
 *
 * This software is provided "as-is," without warranty of any kind, express or
 * implied, including but not limited to the warranties of merchantability,
 * fitness for a particular purpose, and non-infringement. For details, see the
 * full license in the LICENSE file provided with this source code.
 *
 * Non-Military Use Only:
 * This software and its associated components are explicitly intended for
 * civilian and non-military purposes. Use in any military or defense
 * applications is strictly prohibited unless explicitly and individually
 * licensed otherwise by the OpenHD Team.
 *
 * Contributors:
 * A full list of contributors can be found at the OpenHD GitHub repository:
 * https://github.com/OpenHD
 *
 * © OpenHD, All Rights Reserved.
 ******************************************************************************/

// Partition utilities used by SysUtils to replace legacy shell scripts.
//
// Provides helpers for listing block devices, mounting partitions, and
// performing resize operations that previously lived in bash scripts.

#ifndef SYSUTIL_PART_H
#define SYSUTIL_PART_H

#include <optional>
#include <string>
#include <vector>

namespace sysutil {

struct PartitionInfo {
  // Absolute device path, e.g. /dev/mmcblk0p1
  std::string device;
  // UUID reported by blkid/lsblk (may be empty for unformatted partitions)
  std::string uuid;
  // TYPE column from lsblk ("part", "disk", etc.)
  std::string type;
  // Filesystem type reported by lsblk (e.g. ext4, vfat)
  std::string fstype;
  // Human-readable size reported by lsblk (e.g. 14.8G)
  std::string size;
  // Mountpoint if currently mounted, otherwise empty
  std::string mountpoint;
};

// Enumerate partitions by parsing lsblk output.
std::vector<PartitionInfo> list_partitions();

// Ensure a partition is mounted at the requested mount point.
// Creates the mount directory if it does not exist.
bool mount_partition(const std::string& device,
                     const std::string& mount_point,
                     bool read_only = false);

// Locate the device node for a given UUID using blkid.
std::optional<std::string> find_device_by_uuid(const std::string& uuid);

// Resize a partition identified by UUID. This mimics the old
// openhd_resize_util.sh behaviour:
//  - delete and recreate the partition entry with fdisk
//  - refresh the kernel partition table with partprobe
//  - grow the filesystem with resize2fs
bool resize_partition_by_uuid(const std::string& uuid, int partition_number);

// Entry point for partitioning tasks (currently lists partitions only).
bool resize_partition();

// Builds a JSON response with the current partition layout.
std::string build_partitions_response();

// Handles resize requests (placeholder for future partitioning flows).
std::string handle_partition_resize_request(const std::string& choice);

}  // namespace sysutil

#endif  // SYSUTIL_PART_H
