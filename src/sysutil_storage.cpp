/******************************************************************************
 * OpenHD
 *
 * Licensed under the GNU General Public License (GPL) Version 3.
 ******************************************************************************/

#include "sysutil_storage.h"

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include "sysutil_part.h"
#include "sysutil_protocol.h"
#include "sysutil_status.h"

namespace sysutil {
namespace {

constexpr const char* kVideoMountPoint = "/Video";
constexpr const char* kRecordingsLabel = "RECORDINGS";
constexpr std::uint64_t kMigrationReserveBytes = 64ULL * 1024ULL * 1024ULL;
constexpr const char* kRecordingUuidPath =
    "/usr/local/share/OpenHD/SysUtils/recording_uuid";
std::vector<StorageEntry> g_last_inventory;

struct BlockRow {
  std::string device;
  std::string kind;
  std::string parent_device;
  std::string filesystem;
  std::string label;
  std::string mountpoint;
  std::uint64_t size_bytes = 0;
  std::uint64_t start_bytes = 0;
  bool read_only = false;
};

std::string trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string json_escape(const std::string& input) {
  std::string output;
  for (const char c : input) {
    switch (c) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        output += c;
        break;
    }
  }
  return output;
}

bool run_process(const std::vector<std::string>& args) {
  if (args.empty()) {
    return false;
  }
  const pid_t child = ::fork();
  if (child < 0) {
    return false;
  }
  if (child == 0) {
    std::vector<char*> argv;
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    ::execvp(argv.front(), argv.data());
    _exit(127);
  }
  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) {
      return false;
    }
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::optional<std::string> capture_process(
    const std::vector<std::string>& args) {
  if (args.empty()) {
    return std::nullopt;
  }
  int pipe_fds[2];
  if (::pipe(pipe_fds) != 0) {
    return std::nullopt;
  }
  const pid_t child = ::fork();
  if (child < 0) {
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
    return std::nullopt;
  }
  if (child == 0) {
    ::close(pipe_fds[0]);
    ::dup2(pipe_fds[1], STDOUT_FILENO);
    const int dev_null = ::open("/dev/null", O_WRONLY);
    if (dev_null >= 0) {
      ::dup2(dev_null, STDERR_FILENO);
    }
    std::vector<char*> argv;
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    ::execvp(argv.front(), argv.data());
    _exit(127);
  }

  ::close(pipe_fds[1]);
  std::string output;
  char buffer[512];
  ssize_t count = 0;
  while ((count = ::read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
    output.append(buffer, static_cast<std::size_t>(count));
  }
  ::close(pipe_fds[0]);
  int status = 0;
  while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return std::nullopt;
  }
  return output;
}

std::uint64_t parse_u64(const std::string& value) {
  try {
    return value.empty() ? 0 : std::stoull(value);
  } catch (...) {
    return 0;
  }
}

std::uint64_t partition_start_bytes(const std::string& device) {
  const auto name = std::filesystem::path(device).filename().string();
  if (name.empty()) {
    return 0;
  }
  std::ifstream input(std::filesystem::path("/sys/class/block") / name /
                      "start");
  std::uint64_t sectors = 0;
  if (!(input >> sectors) ||
      sectors > std::numeric_limits<std::uint64_t>::max() / 512ULL) {
    return 0;
  }
  // Linux exposes partition offsets in fixed 512-byte sectors in sysfs.
  return sectors * 512ULL;
}

std::vector<BlockRow> read_block_rows() {
  const auto output = capture_process(
      {"lsblk", "-b", "-P", "-p", "-o",
       "PATH,TYPE,SIZE,FSTYPE,LABEL,MOUNTPOINT,PKNAME,RO"});
  if (!output) {
    return {};
  }
  std::vector<BlockRow> rows;
  std::istringstream lines(*output);
  std::string line;
  const std::regex field_re(R"KV((\w+)="([^"]*)")KV");
  while (std::getline(lines, line)) {
    std::map<std::string, std::string> fields;
    for (std::sregex_iterator it(line.begin(), line.end(), field_re), end;
         it != end; ++it) {
      fields[(*it)[1].str()] = (*it)[2].str();
    }
    if (fields["PATH"].empty() ||
        (fields["TYPE"] != "disk" && fields["TYPE"] != "part")) {
      continue;
    }
    BlockRow row;
    row.device = fields["PATH"];
    row.kind = fields["TYPE"] == "part" ? "partition" : "disk";
    row.parent_device = fields["PKNAME"];
    if (!row.parent_device.empty() &&
        row.parent_device.rfind("/dev/", 0) != 0) {
      row.parent_device = "/dev/" + row.parent_device;
    }
    row.size_bytes = parse_u64(fields["SIZE"]);
    row.start_bytes = partition_start_bytes(row.device);
    row.filesystem = fields["FSTYPE"];
    row.label = fields["LABEL"];
    row.mountpoint = fields["MOUNTPOINT"];
    row.read_only = fields["RO"] == "1";
    rows.push_back(std::move(row));
  }
  return rows;
}

std::string canonical_device(const std::string& device) {
  std::error_code ec;
  const auto canonical = std::filesystem::canonical(device, ec);
  return ec ? device : canonical.string();
}

bool is_block_device(const std::string& device) {
  if (device.rfind("/dev/", 0) != 0) {
    return false;
  }
  struct stat st {};
  return ::stat(device.c_str(), &st) == 0 && S_ISBLK(st.st_mode);
}

std::set<std::string> root_devices(const std::vector<BlockRow>& rows) {
  std::set<std::string> roots;
  for (const auto& row : rows) {
    if (row.mountpoint == "/") {
      roots.insert(canonical_device(row.device));
    }
  }
  if (const auto source =
          capture_process({"findmnt", "-n", "-o", "SOURCE", "/"});
      source && trim(*source).rfind("/dev/", 0) == 0) {
    roots.insert(canonical_device(trim(*source)));
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& row : rows) {
      if (roots.count(canonical_device(row.device)) == 0 ||
          row.parent_device.empty()) {
        continue;
      }
      changed |= roots.insert(canonical_device(row.parent_device)).second;
    }
  }
  return roots;
}

std::uint64_t filesystem_free_bytes(const std::string& mountpoint) {
  struct statvfs fs {};
  if (::statvfs(mountpoint.c_str(), &fs) != 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(fs.f_bavail) *
         static_cast<std::uint64_t>(fs.f_frsize);
}

std::uint64_t probe_free_bytes(const BlockRow& row) {
  if (!row.mountpoint.empty()) {
    return filesystem_free_bytes(row.mountpoint);
  }
  if (row.filesystem.empty()) {
    return 0;
  }
  const std::filesystem::path probe =
      std::filesystem::path("/run/openhd/storage-probe") /
      std::filesystem::path(row.device).filename();
  std::error_code ec;
  std::filesystem::create_directories(probe, ec);
  if (ec ||
      !run_process({"mount", "-o", "ro,nosuid,nodev,noexec", row.device,
                    probe.string()})) {
    return 0;
  }
  const auto free_bytes = filesystem_free_bytes(probe.string());
  (void)run_process({"umount", probe.string()});
  std::filesystem::remove(probe, ec);
  return free_bytes;
}

std::optional<StorageEntry> find_current_entry(const StorageEntry& snapshot) {
  for (const auto& current : list_storage_inventory()) {
    if (canonical_device(current.device) == canonical_device(snapshot.device) &&
        current.kind == snapshot.kind) {
      StorageEntry result = current;
      result.id = snapshot.id;
      return result;
    }
  }
  return std::nullopt;
}

std::optional<StorageEntry> snapshot_entry(const int id) {
  for (const auto& entry : g_last_inventory) {
    if (entry.id == id) {
      return find_current_entry(entry);
    }
  }
  // Permit legacy storage ID 1 only when it is the explicit RECORDINGS
  // partition. Never fall back to an arbitrary first disk.
  if (id == 1) {
    const auto current = list_safe_storage();
    for (auto entry : current) {
      if (entry.kind == "partition" &&
          (entry.mounted_at_video || entry.label == kRecordingsLabel)) {
        entry.id = 1;
        return entry;
      }
    }
  }
  return std::nullopt;
}

std::string action_response(const bool ok, const int id,
                            const std::string& action,
                            const std::string& message) {
  std::ostringstream out;
  out << "{\"type\":\"sysutil.storage.action.response\",\"ok\":"
      << (ok ? "true" : "false") << ",\"storage_id\":" << id
      << ",\"action\":\"" << json_escape(action) << "\",\"message\":\""
      << json_escape(message) << "\"}\n";
  return out.str();
}

bool unmount_if_mounted(const StorageEntry& entry) {
  if (entry.mountpoint.empty()) {
    return true;
  }
  ::sync();
  return run_process({"umount", entry.mountpoint});
}

bool persist_recording_device(const std::string& device) {
  const auto uuid =
      capture_process({"blkid", "-s", "UUID", "-o", "value", device});
  if (!uuid || trim(*uuid).empty()) {
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(kRecordingUuidPath).parent_path(), ec);
  if (ec) {
    return false;
  }
  std::ofstream output(kRecordingUuidPath, std::ios::trunc);
  output << trim(*uuid) << "\n";
  return output.good();
}

bool format_partition(const StorageEntry& entry, std::string& message) {
  if (entry.kind != "partition" || !entry.can_format) {
    message = "Selected storage is not a format-capable partition.";
    return false;
  }
  const bool remount_video = entry.mounted_at_video;
  if (!unmount_if_mounted(entry)) {
    message = "Partition is busy and could not be unmounted.";
    return false;
  }
  set_status("storage.format.formatting", "Formatting storage",
             "Creating a FAT32 RECORDINGS filesystem.");
  if (!run_process(
          {"mkfs.vfat", "-F", "32", "-n", kRecordingsLabel, entry.device})) {
    message = "mkfs.vfat failed.";
    return false;
  }
  if (remount_video &&
      !mount_partition(entry.device, kVideoMountPoint, false)) {
    message = "Formatted successfully, but remounting /Video failed.";
    return false;
  }
  if (remount_video) {
    std::ofstream marker("/Video/external_video_part.txt");
    (void)persist_recording_device(entry.device);
  }
  message = remount_video ? "Partition formatted and remounted at /Video."
                          : "Partition formatted successfully.";
  return true;
}

bool mount_for_recording(const StorageEntry& entry, std::string& message) {
  if (entry.kind != "partition" || entry.filesystem.empty() ||
      !entry.can_mount) {
    message = "Selected partition has no mountable filesystem.";
    return false;
  }
  if (entry.mounted_at_video) {
    message = "Partition is already mounted at /Video.";
    return true;
  }

  std::optional<StorageEntry> previous_video;
  for (const auto& candidate : list_safe_storage()) {
    if (candidate.mounted_at_video) {
      previous_video = candidate;
      break;
    }
  }
  if (!unmount_if_mounted(entry)) {
    message = "Selected partition is busy and could not be unmounted.";
    return false;
  }
  if (previous_video && !unmount_if_mounted(*previous_video)) {
    message = "Current /Video partition is busy.";
    return false;
  }
  if (!mount_partition(entry.device, kVideoMountPoint, false)) {
    if (previous_video) {
      (void)mount_partition(previous_video->device, kVideoMountPoint, false);
    }
    message = "Could not mount the selected partition at /Video.";
    return false;
  }
  std::ofstream marker("/Video/external_video_part.txt");
  const bool persisted = persist_recording_device(entry.device);
  message = persisted
                ? "Partition mounted at /Video."
                : "Partition mounted at /Video, but only until the next reboot.";
  return true;
}

bool migrate_recordings(const StorageEntry& entry, std::string& message) {
  if (entry.kind != "partition" || entry.filesystem.empty() ||
      !entry.can_mount || entry.mounted_at_video) {
    message = "Select a different mountable partition for migration.";
    return false;
  }

  const std::filesystem::path source(kVideoMountPoint);
  std::error_code ec;
  if (!std::filesystem::is_directory(source, ec) || ec) {
    message = "The current /Video recording folder is unavailable.";
    return false;
  }

  std::vector<std::filesystem::path> source_files;
  std::uint64_t required_bytes = 0;
  for (std::filesystem::recursive_directory_iterator it(
           source, std::filesystem::directory_options::skip_permission_denied,
           ec),
       end;
       !ec && it != end; it.increment(ec)) {
    const auto& path = it->path();
    if (it.depth() == 0 && path.filename() == "external_video_part.txt") {
      continue;
    }
    if (it->is_regular_file(ec) && !ec) {
      const auto size = it->file_size(ec);
      if (ec || required_bytes >
                    std::numeric_limits<std::uint64_t>::max() - size) {
        message = "Could not calculate the size of existing recordings.";
        return false;
      }
      required_bytes += size;
      source_files.push_back(path);
    }
  }
  if (ec) {
    message = "Could not scan the current recording folder.";
    return false;
  }
  if (source_files.empty()) {
    const bool mounted = mount_for_recording(entry, message);
    if (mounted) {
      message = "No recordings needed moving; the destination is now active.";
    }
    return mounted;
  }

  if (!unmount_if_mounted(entry)) {
    message = "Selected partition is busy and could not be unmounted.";
    return false;
  }
  const auto temporary_mount =
      std::filesystem::path("/run/openhd/storage-migrate") /
      std::to_string(static_cast<int>(entry.id));
  std::filesystem::create_directories(temporary_mount, ec);
  if (ec || !mount_partition(entry.device, temporary_mount.string(), false)) {
    message = "Could not mount the destination for migration.";
    return false;
  }
  const auto cleanup_mount = [&temporary_mount]() {
    (void)run_process({"umount", temporary_mount.string()});
    std::error_code cleanup_ec;
    std::filesystem::remove(temporary_mount, cleanup_ec);
  };

  const auto free_bytes = filesystem_free_bytes(temporary_mount.string());
  if (free_bytes < required_bytes ||
      free_bytes - required_bytes < kMigrationReserveBytes) {
    cleanup_mount();
    message = "Destination does not have enough free space for migration.";
    return false;
  }

  const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  const auto staging =
      temporary_mount / (".openhd-import-" + std::to_string(timestamp));
  const auto completed =
      temporary_mount / ("Imported-OpenHD-" + std::to_string(timestamp));
  std::filesystem::create_directories(staging, ec);
  if (ec) {
    cleanup_mount();
    message = "Could not create a migration folder on the destination.";
    return false;
  }

  set_status("storage.migrating", "Moving Air recordings",
             "Copying and verifying recordings before removing source files.");
  for (const auto& source_file : source_files) {
    const auto relative = std::filesystem::relative(source_file, source, ec);
    if (ec || relative.empty() || relative.string().rfind("..", 0) == 0) {
      std::filesystem::remove_all(staging, ec);
      cleanup_mount();
      message = "Unsafe source path encountered; migration was cancelled.";
      return false;
    }
    const auto destination_file = staging / relative;
    std::filesystem::create_directories(destination_file.parent_path(), ec);
    if (ec || !std::filesystem::copy_file(
                  source_file, destination_file,
                  std::filesystem::copy_options::none, ec) ||
        ec || std::filesystem::file_size(source_file, ec) !=
                  std::filesystem::file_size(destination_file, ec) ||
        ec) {
      std::filesystem::remove_all(staging, ec);
      cleanup_mount();
      message = "Copy or verification failed; source recordings were kept.";
      return false;
    }
  }
  ::sync();
  std::filesystem::rename(staging, completed, ec);
  if (ec) {
    std::filesystem::remove_all(staging, ec);
    cleanup_mount();
    message = "Could not finalize the copied recordings; source files were kept.";
    return false;
  }

  // Remove only regular files that were individually copied and verified.
  for (const auto& source_file : source_files) {
    std::filesystem::remove(source_file, ec);
    if (ec) {
      cleanup_mount();
      message = "Recordings were copied, but some source files could not be removed.";
      return false;
    }
  }
  std::vector<std::filesystem::path> source_directories;
  for (std::filesystem::recursive_directory_iterator it(
           source, std::filesystem::directory_options::skip_permission_denied,
           ec),
       end;
       !ec && it != end; it.increment(ec)) {
    if (it->is_directory(ec) && !ec) {
      source_directories.push_back(it->path());
    }
  }
  std::sort(source_directories.rbegin(), source_directories.rend());
  for (const auto& directory : source_directories) {
    std::filesystem::remove(directory, ec);
    ec.clear();
  }
  ::sync();

  std::optional<StorageEntry> previous_video;
  for (const auto& candidate : list_safe_storage()) {
    if (candidate.mounted_at_video) {
      previous_video = candidate;
      break;
    }
  }
  if (previous_video && !unmount_if_mounted(*previous_video)) {
    cleanup_mount();
    message = "Recordings were copied, but the previous destination is busy.";
    return false;
  }
  cleanup_mount();
  if (!mount_partition(entry.device, kVideoMountPoint, false)) {
    if (previous_video) {
      (void)mount_partition(previous_video->device, kVideoMountPoint, false);
    }
    message = "Recordings were copied, but activating the destination failed.";
    return false;
  }
  std::ofstream marker("/Video/external_video_part.txt");
  const bool persisted = persist_recording_device(entry.device);
  message = persisted
                ? "Recordings moved and the destination is now active."
                : "Recordings moved; destination selection lasts until reboot.";
  return true;
}

std::string first_partition_path(const std::string& disk) {
  return disk.empty() || !std::isdigit(static_cast<unsigned char>(disk.back()))
             ? disk + "1"
             : disk + "p1";
}

bool repartition_disk(const StorageEntry& entry, std::string& message) {
  if (entry.kind != "disk" || !entry.can_repartition) {
    message = "Selected storage is not a repartitionable disk.";
    return false;
  }
  for (const auto& child : list_safe_storage()) {
    if (child.parent_device == entry.device && !child.mountpoint.empty()) {
      if (!unmount_if_mounted(child)) {
        message = "A partition on the disk is busy.";
        return false;
      }
    }
  }
  set_status("storage.repartitioning", "Repartitioning storage",
             "Creating a new GPT partition table.");
  if (!run_process({"parted", "--script", entry.device, "mklabel", "gpt"}) ||
      !run_process({"parted", "--script", entry.device, "mkpart", "primary",
                    "fat32", "1MiB", "100%"}) ||
      !run_process({"partprobe", entry.device})) {
    message = "Creating the partition table failed.";
    return false;
  }
  const auto partition = first_partition_path(entry.device);
  for (int attempt = 0; attempt < 20 && !is_block_device(partition);
       ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  if (!is_block_device(partition) ||
      !run_process(
          {"mkfs.vfat", "-F", "32", "-n", kRecordingsLabel, partition})) {
    message = "Partition created, but FAT32 formatting failed.";
    return false;
  }
  message = "Disk repartitioned with one FAT32 RECORDINGS partition.";
  return true;
}

bool create_recording_partition(const StorageEntry& entry,
                                std::string& message) {
  constexpr std::uint64_t kMinimumBytes = 1024ULL * 1024ULL * 1024ULL;
  constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;
  if (entry.kind != "disk" || !entry.can_create_partition ||
      entry.unallocated_bytes < kMinimumBytes) {
    message = "There is not enough contiguous unallocated space.";
    return false;
  }

  std::set<std::string> existing_partitions;
  for (const auto& row : read_block_rows()) {
    if (row.kind == "partition" &&
        canonical_device(row.parent_device) == canonical_device(entry.device)) {
      existing_partitions.insert(canonical_device(row.device));
    }
  }

  const auto occupied_bytes = entry.size_bytes - entry.unallocated_bytes;
  const auto start_mib = std::max<std::uint64_t>(
      1, (occupied_bytes + kMiB - 1) / kMiB);
  set_status("storage.partition.creating", "Adding recording space",
             "Creating and preparing the recording partition.");
  if (!run_process({"parted", "--script", entry.device, "mkpart", "primary",
                    "fat32", std::to_string(start_mib) + "MiB", "100%"}) ||
      !run_process({"partprobe", entry.device})) {
    message = "Creating the recording partition failed.";
    return false;
  }

  std::optional<BlockRow> created;
  for (int attempt = 0; attempt < 20 && !created; ++attempt) {
    for (const auto& row : read_block_rows()) {
      if (row.kind == "partition" &&
          canonical_device(row.parent_device) == canonical_device(entry.device) &&
          existing_partitions.count(canonical_device(row.device)) == 0) {
        created = row;
        break;
      }
    }
    if (!created) std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  if (!created) {
    message = "The partition was created, but its device did not appear.";
    return false;
  }

  StorageEntry partition;
  partition.id = entry.id;
  partition.device = created->device;
  partition.kind = "partition";
  partition.parent_device = entry.device;
  partition.size_bytes = created->size_bytes;
  partition.can_format = true;
  partition.can_mount = true;
  if (!format_partition(partition, message)) return false;
  partition.filesystem = "vfat";
  if (!mount_for_recording(partition, message)) return false;
  message = "Recording partition created and selected.";
  return true;
}

}  // namespace

std::vector<StorageEntry> list_storage_inventory() {
  const auto rows = read_block_rows();
  const auto roots = root_devices(rows);
  std::set<std::string> excluded_disks;
  for (const auto& row : rows) {
    if (row.kind == "disk" && roots.count(canonical_device(row.device))) {
      excluded_disks.insert(canonical_device(row.device));
    }
  }
  // Fail closed. Without a known root disk we cannot safely assign mutation
  // capabilities to any block device.
  if (excluded_disks.empty()) {
    std::cerr << "[sysutils][storage] Unable to resolve root disk; refusing "
                 "to expose storage devices."
              << std::endl;
    return {};
  }

  std::vector<StorageEntry> entries;
  const bool external_video_mounted = std::any_of(
      rows.begin(), rows.end(), [](const BlockRow& candidate) {
        return candidate.mountpoint == kVideoMountPoint;
      });
  for (const auto& row : rows) {
    const auto disk =
        row.kind == "disk" ? canonical_device(row.device)
                           : canonical_device(row.parent_device);
    const bool is_internal = excluded_disks.count(disk) != 0;
    if (row.read_only || !is_block_device(row.device)) {
      continue;
    }
    StorageEntry entry;
    entry.device = row.device;
    entry.kind = row.kind;
    entry.parent_device = row.parent_device;
    entry.filesystem = row.filesystem;
    entry.label = row.label;
    entry.mountpoint = row.mountpoint;
    entry.size_bytes = row.size_bytes;
    entry.internal = is_internal;
    entry.mounted_at_video = row.mountpoint == kVideoMountPoint ||
                             (is_internal && row.mountpoint == "/" &&
                              !external_video_mounted);
    const bool is_recording_partition =
        row.kind == "partition" &&
        (row.label == kRecordingsLabel || row.mountpoint == kVideoMountPoint);
    const bool is_protected_system_entry =
        is_internal && !is_recording_partition;
    entry.can_format = row.kind == "partition" &&
                       !is_protected_system_entry;
    entry.can_repartition = row.kind == "disk" && !is_internal;
    entry.can_mount = row.kind == "partition" &&
                      !is_protected_system_entry &&
                      !row.filesystem.empty();
    entry.free_bytes =
        row.kind == "partition" ? probe_free_bytes(row) : 0;
    if (row.kind == "disk") {
      std::uint64_t last_end = 0;
      for (const auto& child : rows) {
        if (child.kind != "partition" ||
            canonical_device(child.parent_device) !=
                canonical_device(row.device)) {
          continue;
        }
        const auto end = child.start_bytes + child.size_bytes;
        last_end = std::max(last_end, end);
      }
      entry.unallocated_bytes = row.size_bytes > last_end
                                    ? row.size_bytes - last_end
                                    : 0;
      entry.can_create_partition =
          entry.unallocated_bytes >= 1024ULL * 1024ULL * 1024ULL;
      // Shrinking a mounted root filesystem is not safe. This remains false
      // until an offline, reboot-assisted implementation is available.
      entry.can_resize_partition = false;
    }
    entries.push_back(std::move(entry));
  }

  std::sort(entries.begin(), entries.end(),
            [](const StorageEntry& lhs, const StorageEntry& rhs) {
              return lhs.device < rhs.device;
            });
  std::uint16_t next_id = 2;
  for (auto& entry : entries) {
    if (entry.kind == "partition" &&
        (entry.mounted_at_video || entry.label == kRecordingsLabel)) {
      entry.id = 1;
      break;
    }
  }
  for (auto& entry : entries) {
    if (entry.id == 0 && next_id <= 254) {
      entry.id = static_cast<std::uint8_t>(next_id++);
    }
  }
  entries.erase(
      std::remove_if(entries.begin(), entries.end(),
                     [](const StorageEntry& entry) { return entry.id == 0; }),
      entries.end());
  return entries;
}

std::vector<StorageEntry> list_safe_storage() {
  auto entries = list_storage_inventory();
  entries.erase(
      std::remove_if(entries.begin(), entries.end(),
                     [](const StorageEntry& entry) {
                       return entry.internal && !entry.can_format &&
                              !entry.can_mount;
                     }),
      entries.end());
  return entries;
}

bool is_storage_list_request(const std::string& line) {
  const auto type = extract_string_field(line, "type");
  return type && *type == "sysutil.storage.list.request";
}

std::string build_storage_list_response() {
  g_last_inventory = list_storage_inventory();
  std::ostringstream out;
  out << "{\"type\":\"sysutil.storage.list.response\",\"entries\":[";
  for (std::size_t i = 0; i < g_last_inventory.size(); ++i) {
    const auto& entry = g_last_inventory[i];
    if (i != 0) {
      out << ",";
    }
    out << "{\"id\":" << static_cast<int>(entry.id)
        << ",\"device\":\"" << json_escape(entry.device)
        << "\",\"kind\":\"" << entry.kind << "\",\"parent_device\":\""
        << json_escape(entry.parent_device) << "\",\"filesystem\":\""
        << json_escape(entry.filesystem) << "\",\"label\":\""
        << json_escape(entry.label) << "\",\"mountpoint\":\""
        << json_escape(entry.mountpoint) << "\",\"size_bytes\":"
        << entry.size_bytes << ",\"free_bytes\":" << entry.free_bytes
        << ",\"unallocated_bytes\":" << entry.unallocated_bytes
        << ",\"internal\":" << (entry.internal ? "true" : "false")
        << ",\"mounted_at_video\":"
        << (entry.mounted_at_video ? "true" : "false")
        << ",\"can_format\":" << (entry.can_format ? "true" : "false")
        << ",\"can_repartition\":"
        << (entry.can_repartition ? "true" : "false")
        << ",\"can_mount\":" << (entry.can_mount ? "true" : "false")
        << ",\"can_create_partition\":"
        << (entry.can_create_partition ? "true" : "false")
        << ",\"can_resize_partition\":"
        << (entry.can_resize_partition ? "true" : "false")
        << "}";
  }
  out << "]}\n";
  return out.str();
}

bool is_storage_action_request(const std::string& line) {
  const auto type = extract_string_field(line, "type");
  return type && *type == "sysutil.storage.action.request";
}

std::string handle_storage_action_request(const std::string& line) {
  const int id = extract_int_field(line, "storage_id").value_or(0);
  const auto action = extract_string_field(line, "action").value_or("");
  if (!extract_bool_field(line, "confirm").value_or(false)) {
    return action_response(false, id, action, "Confirmation is required.");
  }
  const auto entry = snapshot_entry(id);
  if (!entry) {
    return action_response(false, id, action,
                           "Storage ID is stale, unsafe, or unavailable.");
  }

  std::string message;
  bool ok = false;
  if (action == "format") {
    ok = format_partition(*entry, message);
  } else if (action == "repartition") {
    ok = repartition_disk(*entry, message);
  } else if (action == "mount") {
    ok = mount_for_recording(*entry, message);
  } else if (action == "migrate") {
    ok = migrate_recordings(*entry, message);
  } else if (action == "create") {
    ok = create_recording_partition(*entry, message);
  } else if (action == "resize") {
    message = "Resizing the system partition requires offline maintenance.";
  } else {
    message = "Unsupported storage action.";
  }
  set_status(ok ? "storage.action.complete" : "storage.action.failed",
             ok ? "Storage operation complete" : "Storage operation failed",
             message, ok ? 0 : 2);
  g_last_inventory = list_storage_inventory();
  return action_response(ok, id, action, message);
}

bool is_storage_format_request(const std::string& line) {
  const auto type = extract_string_field(line, "type");
  return type && *type == "sysutil.storage.format.request";
}

std::string handle_storage_format_request(const std::string& line) {
  std::ostringstream translated;
  translated << "{\"type\":\"sysutil.storage.action.request\","
             << "\"action\":\"format\",\"storage_id\":"
             << extract_int_field(line, "storage_id").value_or(0)
             << ",\"confirm\":"
             << (extract_bool_field(line, "confirm").value_or(false) ? "true"
                                                                     : "false")
             << "}";
  const auto response = handle_storage_action_request(translated.str());
  std::string compatible = response;
  const auto pos = compatible.find("sysutil.storage.action.response");
  if (pos != std::string::npos) {
    compatible.replace(pos, std::strlen("sysutil.storage.action.response"),
                       "sysutil.storage.format.response");
  }
  return compatible;
}

}  // namespace sysutil
