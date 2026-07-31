/******************************************************************************
 * OpenHD
 *
 * Licensed under the GNU General Public License (GPL) Version 3.
 ******************************************************************************/

#ifndef SYSUTIL_STORAGE_H
#define SYSUTIL_STORAGE_H

#include <cstdint>
#include <string>
#include <vector>

namespace sysutil {

struct StorageEntry {
  std::uint8_t id = 0;
  std::string device;
  std::string kind;  // "disk" or "partition"
  std::string parent_device;
  std::string filesystem;
  std::string label;
  std::string mountpoint;
  std::uint64_t size_bytes = 0;
  std::uint64_t free_bytes = 0;
  bool mounted_at_video = false;
  bool can_format = false;
  bool can_repartition = false;
  bool can_mount = false;
};

// Returns non-root storage devices. The complete disk containing / is omitted.
std::vector<StorageEntry> list_safe_storage();

bool is_storage_list_request(const std::string& line);
std::string build_storage_list_response();

// action is one of: format, repartition, mount.
bool is_storage_action_request(const std::string& line);
std::string handle_storage_action_request(const std::string& line);

// Backward-compatible MAV_CMD_STORAGE_FORMAT socket request.
bool is_storage_format_request(const std::string& line);
std::string handle_storage_format_request(const std::string& line);

}  // namespace sysutil

#endif  // SYSUTIL_STORAGE_H
