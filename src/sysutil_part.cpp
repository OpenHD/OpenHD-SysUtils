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

#include "sysutil_status.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/mount.h>
#include <sys/statvfs.h>
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

std::string blkid_value(const std::string& device, const std::string& key) {
  auto output =
      run_command("blkid -o value -s " + key + " " + device + " 2>/dev/null");
  if (!output) {
    return {};
  }
  return trim(*output);
}

std::string json_escape(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

long long parse_ll(const std::string& value) {
  if (value.empty()) {
    return 0;
  }
  try {
    return std::stoll(value);
  } catch (...) {
    return 0;
  }
}

struct LsblkRow {
  std::string name;
  std::string type;
  long long size_bytes = 0;
  long long start_bytes = 0;
  std::string fstype;
  std::string label;
  std::string mountpoint;
  std::string parent;
};

std::optional<std::string> run_lsblk_output(
    const std::vector<std::string>& commands) {
  for (const auto& command : commands) {
    auto output = run_command(command);
    if (output && !trim(*output).empty()) {
      return output;
    }
  }
  return std::nullopt;
}

struct LsblkResult {
  std::vector<LsblkRow> rows;
  bool has_start = false;
  bool has_parent = false;
};

std::optional<std::string> base_device_from_name(const std::string& name) {
  std::regex re(R"(^(.+?)(p?)(\d+)$)");
  std::smatch match;
  if (!std::regex_match(name, match, re)) {
    return std::nullopt;
  }
  return match[1].str();
}

LsblkResult read_lsblk_rows() {
  LsblkResult result;
  const std::vector<std::string> commands = {
      "lsblk -b -P -o NAME,TYPE,SIZE,START,FSTYPE,LABEL,MOUNTPOINT,PKNAME 2>/dev/null",
      "lsblk -b -P -o NAME,TYPE,SIZE,START,LABEL,PKNAME 2>/dev/null",
      "lsblk -b -P -o NAME,TYPE,SIZE,LABEL,PKNAME 2>/dev/null",
      "lsblk -b -P -o NAME,TYPE,SIZE 2>/dev/null"};
  const auto output = run_lsblk_output(commands);
  if (!output) {
    return result;
  }

  result.has_start = output->find("START=") != std::string::npos;
  result.has_parent = output->find("PKNAME=") != std::string::npos;

  std::istringstream iss(*output);
  std::string line;
  while (std::getline(iss, line)) {
    std::map<std::string, std::string> fields;
    std::regex token_re(R"((\w+)=\"([^\"]*)\")");
    for (std::sregex_iterator it(line.begin(), line.end(), token_re), end;
         it != end; ++it) {
      fields[(*it)[1].str()] = (*it)[2].str();
    }

    if (fields.find("NAME") == fields.end() ||
        fields.find("TYPE") == fields.end()) {
      continue;
    }

    LsblkRow row;
    row.name = fields["NAME"];
    row.type = fields["TYPE"];
    row.size_bytes = parse_ll(fields["SIZE"]);
    row.start_bytes = parse_ll(fields["START"]);
    row.fstype = fields["FSTYPE"];
    row.label = fields["LABEL"];
    row.mountpoint = fields["MOUNTPOINT"];
    row.parent = fields["PKNAME"];

    if (row.type == "part") {
      const std::string device = "/dev/" + row.name;
      if (row.fstype.empty()) {
        row.fstype = blkid_value(device, "TYPE");
      }
      if (row.label.empty()) {
        row.label = blkid_value(device, "LABEL");
      }
    }

    result.rows.push_back(std::move(row));
  }

  return result;
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

std::optional<int> partition_number_from_device(
    const std::string& partition_device) {
  std::regex re(R"(^(.+?)(p?)(\d+)$)");
  std::smatch match;
  if (!std::regex_match(partition_device, match, re)) {
    return std::nullopt;
  }
  try {
    return std::stoi(match[3].str());
  } catch (...) {
    return std::nullopt;
  }
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
  const std::vector<std::string> commands = {
      "lsblk -P -o NAME,UUID,TYPE,MOUNTPOINT,FSTYPE,SIZE 2>/dev/null",
      "lsblk -P -o NAME,UUID,TYPE,MOUNTPOINT,SIZE 2>/dev/null",
      "lsblk -P -o NAME,UUID,TYPE,SIZE 2>/dev/null",
      "lsblk -P -o NAME,TYPE,SIZE 2>/dev/null"};
  auto output = run_lsblk_output(commands);
  if (!output) {
    return result;
  }

  std::istringstream iss(*output);
  std::string line;
  while (std::getline(iss, line)) {
    PartitionInfo info;
    std::regex token_re(
        R"((NAME|UUID|TYPE|MOUNTPOINT|FSTYPE|SIZE)=\"([^\"]*)\")");
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
      } else if (key == "FSTYPE") {
        info.fstype = value;
      } else if (key == "SIZE") {
        info.size = value;
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
bool resize_partition_by_uuid(const std::string& uuid, int partition_number) {
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

bool run_fdisk_type_fat32(const std::string& base_device, int partition_number) {
  std::ostringstream cmd;
  cmd << "sh -c \"printf 't\\n" << partition_number
      << "\\n0c\\nw\\n' | fdisk " << base_device << "\"";
  int ret = std::system(cmd.str().c_str());
  if (ret != 0) {
    std::cerr << "fdisk type change failed with code " << ret << std::endl;
    return false;
  }
  return true;
}

bool run_shell_command(const std::string& command) {
  int ret = std::system(command.c_str());
  if (ret != 0) {
    std::cerr << "Command failed (" << ret << "): " << command << std::endl;
    return false;
  }
  return true;
}

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool is_fat32(std::string fstype) {
  fstype = to_lower(std::move(fstype));
  return fstype == "vfat" || fstype == "fat32" || fstype == "fat";
}

bool is_label(const std::string& label, const std::string& expected) {
  if (label.empty()) {
    return false;
  }
  return to_lower(label) == to_lower(expected);
}

long long filesystem_free_bytes(const std::string& mountpoint) {
  struct statvfs st {};
  if (statvfs(mountpoint.c_str(), &st) != 0) {
    return 0;
  }
  return static_cast<long long>(st.f_bavail) *
         static_cast<long long>(st.f_frsize);
}

bool resize_partition() {
  set_status("partitioning", "Listing partitions", "Preparing partition tasks.");
  const auto partitions = list_partitions();
  if (partitions.empty()) {
    std::cout << "No partitions found." << std::endl;
    return false;
  }

  for (const auto& part : partitions) {
    std::cout << "Partition: " << part.device
              << " | Size: " << (part.size.empty() ? "-" : part.size)
              << " | FSType: " << (part.fstype.empty() ? "-" : part.fstype)
              << " | Mount: " << (part.mountpoint.empty() ? "-" : part.mountpoint)
              << std::endl;
  }

  return true;
}

struct ResizeCandidate {
  std::string disk_name;
  std::string part_name;
  std::string device;
  std::string fstype;
  std::string label;
  long long start_bytes = 0;
  long long size_bytes = 0;
  long long free_after = 0;
};

std::optional<ResizeCandidate> find_resize_candidate(const LsblkResult& result) {
  const auto& rows = result.rows;
  std::optional<ResizeCandidate> best;

  for (const auto& disk : rows) {
    if (disk.type != "disk") {
      continue;
    }
    std::vector<LsblkRow> parts;
    for (const auto& row : rows) {
      if (row.type != "part") {
        continue;
      }
      if (result.has_parent) {
        if (row.parent == disk.name) {
          parts.push_back(row);
        }
      } else {
        auto base = base_device_from_name(row.name);
        if (base && *base == disk.name) {
          parts.push_back(row);
        }
      }
    }

    if (parts.empty()) {
      continue;
    }

    if (result.has_start) {
      std::sort(parts.begin(), parts.end(),
                [](const LsblkRow& a, const LsblkRow& b) {
                  return a.start_bytes < b.start_bytes;
                });
    } else {
      std::sort(parts.begin(), parts.end(),
                [](const LsblkRow& a, const LsblkRow& b) {
                  return a.name < b.name;
                });
      long long cursor = 0;
      for (auto& part : parts) {
        part.start_bytes = cursor;
        cursor += part.size_bytes;
      }
    }

    for (std::size_t i = 0; i < parts.size(); ++i) {
      const auto& part = parts[i];
      const bool is_last = (i + 1 == parts.size());
      const bool is_unformatted = part.fstype.empty();
      if (!is_fat32(part.fstype) && !(is_last && is_unformatted)) {
        continue;
      }
      long long end = part.start_bytes + part.size_bytes;
      long long free_after = 0;
      if (i + 1 < parts.size()) {
        free_after = parts[i + 1].start_bytes - end;
      } else if (disk.size_bytes > 0 && disk.size_bytes > end) {
        free_after = disk.size_bytes - end;
      }
      if (free_after <= 0) {
        continue;
      }

      ResizeCandidate candidate;
      candidate.disk_name = disk.name;
      candidate.part_name = part.name;
      candidate.device = "/dev/" + part.name;
      candidate.fstype = part.fstype;
      candidate.label = part.label;
      candidate.start_bytes = part.start_bytes;
      candidate.size_bytes = part.size_bytes;
      candidate.free_after = free_after;

      if (!best || candidate.start_bytes > best->start_bytes) {
        best = std::move(candidate);
      }
    }
  }

  return best;
}

bool ensure_fstab_entry(const std::string& device, const std::string& mountpoint,
                        const std::string& fstype) {
  std::ifstream fstab("/etc/fstab");
  std::string line;
  while (std::getline(fstab, line)) {
    if (line.find(device) != std::string::npos &&
        line.find(mountpoint) != std::string::npos) {
      return true;
    }
  }
  std::ofstream out("/etc/fstab", std::ios::app);
  if (!out) {
    std::cerr << "Failed to open /etc/fstab for append." << std::endl;
    return false;
  }
  out << device << "  " << mountpoint << "  " << fstype
      << "  defaults  0  2\n";
  return true;
}

void mount_known_partitions() {
  const auto result = read_lsblk_rows();
  for (const auto& row : result.rows) {
    if (row.type != "part") {
      continue;
    }
    const std::string device = "/dev/" + row.name;
    if (is_label(row.label, "recordings")) {
      (void)mount_partition(device, "/Video", false);
    } else if (is_label(row.label, "openhd")) {
      (void)mount_partition(device, "/Config", false);
    }
  }
}

bool resize_fat32_partition(const ResizeCandidate& candidate) {
  const std::string partition_device = candidate.device;
  auto base_device_opt = base_device_for_partition(partition_device);
  if (!base_device_opt) {
    std::cerr << "Unable to determine base device for " << partition_device
              << std::endl;
    return false;
  }
  auto part_number_opt = partition_number_from_device(partition_device);
  if (!part_number_opt) {
    std::cerr << "Unable to determine partition number for "
              << partition_device << std::endl;
    return false;
  }

  const std::string base_device = *base_device_opt;
  const int part_number = *part_number_opt;

  std::error_code ec;
  std::filesystem::create_directories("/run/openhd", ec);
  std::ofstream hold("/run/openhd/hold.pid");
  hold.close();

  set_status("partitioning", "Formatting", "Preparing FAT32 filesystem.");
  if (!run_shell_command("mkfs.fat -F 32 " + partition_device)) {
    return false;
  }

  set_status("partitioning", "Resizing", "Expanding partition.");
  std::ostringstream resize_cmd;
  resize_cmd << "parted " << base_device << " --script resizepart "
             << part_number << " 100%";
  if (!run_shell_command(resize_cmd.str())) {
    return false;
  }

  set_status("partitioning", "Formatting", "Applying volume label.");
  if (!run_shell_command("mkfs.vfat -F 32 -n \"RECORDINGS\" " +
                         partition_device)) {
    return false;
  }

  set_status("partitioning", "Updating table", "Setting FAT32 LBA type.");
  if (!run_fdisk_type_fat32(base_device, part_number)) {
    return false;
  }

  set_status("partitioning", "Configuring", "Updating fstab and markers.");
  std::filesystem::create_directories("/Video", ec);

  if (!ensure_fstab_entry(partition_device, "/Video", "auto")) {
    return false;
  }

  if (!mount_partition(partition_device, "/Video", false)) {
    return false;
  }

  std::ofstream marker("/Video/external_video_part.txt");
  marker.close();

  set_status("partitioning", "Complete", "Rebooting after resize.");
  (void)run_shell_command("reboot");
  return true;
}

std::string build_partitions_response() {
  const auto result = read_lsblk_rows();
  const auto& rows = result.rows;
  const auto candidate = find_resize_candidate(result);
  std::ostringstream out;
  out << "{\"type\":\"sysutil.partitions.response\",\"disks\":[";

  bool first_disk = true;
  for (const auto& disk : rows) {
    if (disk.type != "disk") {
      continue;
    }
    if (!first_disk) {
      out << ",";
    }
    first_disk = false;

    std::vector<LsblkRow> parts;
    for (const auto& row : rows) {
      if (row.type != "part") {
        continue;
      }
      if (result.has_parent) {
        if (row.parent == disk.name) {
          parts.push_back(row);
        }
      } else {
        auto base = base_device_from_name(row.name);
        if (base && *base == disk.name) {
          parts.push_back(row);
        }
      }
    }
    if (result.has_start) {
      std::sort(parts.begin(), parts.end(),
                [](const LsblkRow& a, const LsblkRow& b) {
                  return a.start_bytes < b.start_bytes;
                });
    } else {
      std::sort(parts.begin(), parts.end(),
                [](const LsblkRow& a, const LsblkRow& b) {
                  return a.name < b.name;
                });
      long long cursor = 0;
      for (auto& part : parts) {
        part.start_bytes = cursor;
        cursor += part.size_bytes;
      }
    }

    const long long disk_size =
        disk.size_bytes > 0 ? disk.size_bytes : 0;
    out << "{\"name\":\"/dev/" << json_escape(disk.name) << "\""
        << ",\"sizeBytes\":" << disk_size << ",\"segments\":[";

    long long cursor = 0;
    bool first_segment = true;
    for (const auto& part : parts) {
      if (part.start_bytes > cursor) {
        if (!first_segment) {
          out << ",";
        }
        first_segment = false;
        out << "{\"kind\":\"free\",\"startBytes\":" << cursor
            << ",\"sizeBytes\":" << (part.start_bytes - cursor) << "}";
      }

      if (!first_segment) {
        out << ",";
      }
      first_segment = false;
      out << "{\"kind\":\"partition\",\"device\":\"/dev/"
          << json_escape(part.name) << "\"";
      if (!part.mountpoint.empty()) {
        out << ",\"mountpoint\":\"" << json_escape(part.mountpoint) << "\"";
      }
      if (!part.fstype.empty()) {
        out << ",\"fstype\":\"" << json_escape(part.fstype) << "\"";
      }
      if (!part.label.empty()) {
        out << ",\"label\":\"" << json_escape(part.label) << "\"";
      }
      out << ",\"startBytes\":" << part.start_bytes
          << ",\"sizeBytes\":" << part.size_bytes << "}";

      cursor = part.start_bytes + part.size_bytes;
    }

    if (disk_size > cursor) {
      if (!first_segment) {
        out << ",";
      }
      out << "{\"kind\":\"free\",\"startBytes\":" << cursor
          << ",\"sizeBytes\":" << (disk_size - cursor) << "}";
    }

    out << "],\"partitions\":[";
    bool first_part = true;
    for (const auto& part : parts) {
      if (!first_part) {
        out << ",";
      }
      first_part = false;
      const std::string part_device = "/dev/" + part.name;
      long long free_bytes = 0;
      if (is_label(part.label, "recordings")) {
        std::string mountpoint = part.mountpoint.empty() ? "/Video" : part.mountpoint;
        (void)mount_partition(part_device, mountpoint, false);
        free_bytes = filesystem_free_bytes(mountpoint);
      }

      out << "{\"device\":\"/dev/" << json_escape(part.name) << "\"";
      if (!part.mountpoint.empty()) {
        out << ",\"mountpoint\":\"" << json_escape(part.mountpoint) << "\"";
      }
      if (!part.fstype.empty()) {
        out << ",\"fstype\":\"" << json_escape(part.fstype) << "\"";
      }
      if (!part.label.empty()) {
        out << ",\"label\":\"" << json_escape(part.label) << "\"";
      }
      if (free_bytes > 0) {
        out << ",\"freeBytes\":" << free_bytes;
      }
      out << ",\"startBytes\":" << part.start_bytes
          << ",\"sizeBytes\":" << part.size_bytes << "}";
    }
    out << "]}";
  }

  out << "],\"resizable\":";
  if (candidate) {
    out << "{\"device\":\"" << json_escape(candidate->device) << "\"";
    if (!candidate->label.empty()) {
      out << ",\"label\":\"" << json_escape(candidate->label) << "\"";
    }
    if (!candidate->fstype.empty()) {
      out << ",\"fstype\":\"" << json_escape(candidate->fstype) << "\"";
    }
    out << ",\"freeBytes\":" << candidate->free_after << "}";
  } else {
    out << "null";
  }
  out << "}\n";
  return out.str();
}

std::string handle_partition_resize_request(const std::string& choice) {
  const bool wants_resize = (choice == "yes" || choice == "true" ||
                             choice == "1");
  const auto candidate = find_resize_candidate(read_lsblk_rows());
  if (!candidate) {
    set_status("partitioning", "Not resizable",
               "No FAT32 partition with free space.");
    return "{\"type\":\"sysutil.partition.resize.response\",\"accepted\":false}\n";
  }

  if (!wants_resize) {
    set_status("partitioning", "Resize skipped",
               "Partitioning was not requested.");
    return "{\"type\":\"sysutil.partition.resize.response\",\"accepted\":true}\n";
  }

  set_status("partitioning", "Resize requested",
             "Preparing to resize FAT32 partition.");
  if (!resize_fat32_partition(*candidate)) {
    set_status("partitioning", "Resize failed",
               "Partition resize did not complete.");
    return "{\"type\":\"sysutil.partition.resize.response\",\"accepted\":false}\n";
  }

  return "{\"type\":\"sysutil.partition.resize.response\",\"accepted\":true}\n";
}

}  // namespace sysutil
