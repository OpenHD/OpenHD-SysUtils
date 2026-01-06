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

#include "sysutil_part.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace sysutil {
namespace {

// Runs a command and captures stdout.
std::optional<std::string> run_command(const std::string& command) {
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) {
    std::perror("popen");
    return std::nullopt;
  }
  std::string output;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe)) {
    output += buffer;
  }
  const int status = pclose(pipe);
  if (status == -1) {
    std::perror("pclose");
    return std::nullopt;
  }
  return output;
}

// Trims whitespace from both ends of a string.
std::string trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\n\r");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\n\r");
  return value.substr(begin, end - begin + 1);
}

// Checks whether a device is already mounted at a mountpoint.
bool is_already_mounted(const std::string& device,
                        const std::string& mount_point) {
  std::ifstream mounts("/proc/mounts");
  std::string line;
  while (std::getline(mounts, line)) {
    std::istringstream iss(line);
    std::string dev, mnt;
    if (!(iss >> dev >> mnt)) {
      continue;
    }
    if (dev == device && mnt == mount_point) {
      return true;
    }
  }
  return false;
}

// Derives the base device name from a partition device path.
std::optional<std::string> base_device_for_partition(
    const std::string& partition_device) {
  std::regex re(R"(^(.+?)(p?)(\d+)$)");
  std::smatch match;
  if (!std::regex_match(partition_device, match, re)) {
    return std::nullopt;
  }
  return match[1].str();
}

// Resizes a partition using fdisk in a scripted manner.
bool run_fdisk_resize(const std::string& base_device, int partition_number) {
  std::ostringstream fdisk_cmd;
  fdisk_cmd << "fdisk " << base_device;

  FILE* pipe = popen(fdisk_cmd.str().c_str(), "w");
  if (!pipe) {
    std::perror("popen fdisk");
    return false;
  }

  // Delete and recreate the partition to fill the device, mirroring the
  // legacy shell behaviour.
  std::ostringstream script;
  script << "d\n" << partition_number << "\n";
  script << "n\n" << partition_number << "\n\n\n";
  script << "w\n";

  const std::string data = script.str();
  const size_t written = fwrite(data.data(), 1, data.size(), pipe);
  if (written != data.size()) {
    std::perror("fwrite fdisk");
    pclose(pipe);
    return false;
  }
  if (fflush(pipe) != 0) {
    std::perror("fflush fdisk");
    pclose(pipe);
    return false;
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "fdisk returned non-zero status: " << status << std::endl;
    return false;
  }
  return true;
}

// Grows the filesystem with resize2fs.
bool run_resize2fs(const std::string& device_by_uuid) {
  std::string command = "resize2fs " + device_by_uuid;
  int ret = std::system(command.c_str());
  if (ret != 0) {
    std::cerr << "resize2fs failed with code " << ret << std::endl;
    return false;
  }
  return true;
}

}  // namespace

// Lists block device partitions using lsblk output parsing.
std::vector<PartitionInfo> list_partitions() {
  std::vector<PartitionInfo> result;
  auto output = run_command("lsblk -P -o NAME,UUID,TYPE,MOUNTPOINT");
  if (!output) {
    return result;
  }

  std::istringstream iss(*output);
  std::string line;
  while (std::getline(iss, line)) {
    PartitionInfo info;
    std::regex token_re(R"((NAME|UUID|TYPE|MOUNTPOINT)=\"([^\"]*)\")");
    for (std::sregex_iterator it(line.begin(), line.end(), token_re), end;
         it != end; ++it) {
      const std::string key = (*it)[1];
      const std::string value = (*it)[2];
      if (key == "NAME") {
        info.device = "/dev/" + value;
      } else if (key == "UUID") {
        info.uuid = value;
      } else if (key == "TYPE") {
        info.type = value;
      } else if (key == "MOUNTPOINT") {
        info.mountpoint = value;
      }
    }
    if (!info.device.empty() && info.type == "part") {
      result.push_back(std::move(info));
    }
  }
  return result;
}

// Mounts a partition at the requested path, optionally read-only.
bool mount_partition(const std::string& device,
                     const std::string& mount_point,
                     bool read_only) {
  std::error_code ec;
  std::filesystem::create_directories(mount_point, ec);
  if (ec) {
    std::cerr << "Failed to create mount point " << mount_point << ": "
              << ec.message() << std::endl;
    return false;
  }

  if (is_already_mounted(device, mount_point)) {
    return true;
  }

  unsigned long flags = MS_RELATIME;
  if (read_only) {
    flags |= MS_RDONLY;
  }

  if (::mount(device.c_str(), mount_point.c_str(), nullptr, flags, nullptr) ==
      0) {
    return true;
  }

  // Fallback to /sbin/mount for filesystems that require helpers.
  std::string cmd = std::string("mount ") + (read_only ? "-o ro " : "") +
                    device + " " + mount_point;
  int ret = std::system(cmd.c_str());
  if (ret != 0) {
    std::cerr << "Failed to mount " << device << " at " << mount_point
              << " (code " << ret << ")" << std::endl;
    return false;
  }
  return true;
}

// Finds the device path for a given UUID.
std::optional<std::string> find_device_by_uuid(const std::string& uuid) {
  auto output = run_command("blkid -l -o device -t UUID=\"" + uuid + "\"");
  if (!output) {
    return std::nullopt;
  }
  auto path = trim(*output);
  if (path.empty()) {
    return std::nullopt;
  }
  return path;
}

// Resizes a partition and filesystem for a UUID.
bool resize_partition(const std::string& uuid, int partition_number) {
  auto device_path_opt = find_device_by_uuid(uuid);
  if (!device_path_opt) {
    std::cerr << "Partition with UUID " << uuid << " not found." << std::endl;
    return false;
  }

  std::string partition_device = *device_path_opt;
  std::string real_device = partition_device;
  std::error_code ec;
  auto canonical = std::filesystem::weakly_canonical(partition_device, ec);
  if (!ec) {
    real_device = canonical.string();
  }

  auto base_device_opt = base_device_for_partition(real_device);
  if (!base_device_opt) {
    std::cerr << "Unable to determine base device for " << real_device
              << std::endl;
    return false;
  }
  std::string base_device = *base_device_opt;

  if (!run_fdisk_resize(base_device, partition_number)) {
    return false;
  }

  // Refresh partition table
  std::string partprobe_cmd = "partprobe " + partition_device;
  int partprobe_ret = std::system(partprobe_cmd.c_str());
  if (partprobe_ret != 0) {
    std::cerr << "partprobe failed with code " << partprobe_ret << std::endl;
    return false;
  }

  const std::string device_by_uuid = "/dev/disk/by-uuid/" + uuid;
  if (!run_resize2fs(device_by_uuid)) {
    return false;
  }

  std::cout << "Partition resized and filesystem expanded." << std::endl;
  return true;
}

// Runs resize only when a request flag file exists.
bool resize_partition_if_requested(
    const std::string& uuid,
    int partition_number,
    const std::vector<std::string>& request_files) {
  bool requested = false;
  for (const auto& path : request_files) {
    if (std::filesystem::exists(path)) {
      requested = true;
      break;
    }
  }

  if (!requested) {
    std::cout << "Resize not requested. No flag file found." << std::endl;
    return false;
  }

  if (!resize_partition(uuid, partition_number)) {
    return false;
  }

  for (const auto& path : request_files) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  return true;
}

}  // namespace sysutil
