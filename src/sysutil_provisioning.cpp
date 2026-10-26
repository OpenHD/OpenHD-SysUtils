// SPDX-License-Identifier: Apache-2.0
#include "sysutil_provisioning.h"
#include "openhdprovisioning.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;
namespace {
std::string read_text(const fs::path& path) {
    std::ifstream input(path); std::string value;
    std::getline(input, value); return value;
}
bool real_directory(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(fs::symlink_status(path, ec)) && !ec;
}
bool atomic_write(const fs::path& path, const std::string& data) {
    // O_NOFOLLOW and O_EXCL prevent following an existing temporary symlink.
    const auto temporary = path.string() + ".provisioning-" + std::to_string(getpid());
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    std::size_t written = 0;
    while (written < data.size()) {
        const auto count = ::write(fd, data.data() + written, data.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        written += count;
    }
    bool ok = written == data.size() && ::fsync(fd) == 0;
    if (::close(fd) != 0) ok = false;
    if (ok) ok = ::rename(temporary.c_str(), path.c_str()) == 0;
    if (!ok) { ::unlink(temporary.c_str()); return false; }
    const int directory = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (directory < 0) return false;
    ok = ::fsync(directory) == 0;
    ::close(directory);
    return ok;
}
}

bool sysutil::apply_provisioning(const std::string& capsule, const fs::path& config,
                                 std::string& error) {
    std::vector<openhd_provisioning::File> files;
    if (!openhd_provisioning::decode(capsule, files)) { error = "Invalid provisioning capsule"; return false; }
    if (files.empty()) return true; // Factory placeholder, no user selections.
    if (!real_directory(config)) { error = "Config directory is unavailable"; return false; }
    const auto marker = config / ".imagewriter-provisioned";
    // Identity includes all capsule bytes; store the small CRC/length header.
    const std::string identity = capsule.substr(0, 16);
    std::ifstream previous(marker, std::ios::binary);
    std::string previous_identity(17, '\0');
    previous.read(&previous_identity[0], previous_identity.size());
    previous_identity.resize(previous.gcount());
    if (previous_identity == identity) return true;
    // Validate all parents before changing any configuration. Never follow a
    // symlink into runtime credentials or another mounted filesystem.
    for (const auto& file : files) {
        auto parent = config;
        const auto target = config / fs::u8path(file.path);
        for (const auto& component : fs::u8path(file.path).parent_path()) {
            parent /= component;
            std::error_code ec;
            if (fs::exists(fs::symlink_status(parent, ec))) {
                if (!real_directory(parent)) { error = "Unsafe provisioning directory"; return false; }
            } else if (!fs::create_directory(parent, ec) || ec) { error = "Cannot create provisioning directory"; return false; }
        }
        std::error_code ec;
        const auto status = fs::symlink_status(target, ec);
        if (fs::exists(status) && !fs::is_regular_file(status)) { error = "Unsafe provisioning destination"; return false; }
    }
    for (const auto& file : files) {
        if (file.path != "openhd/air.txt" && file.path != "openhd/ground.txt") continue;
        for (const auto* role : {"air.txt", "ground.txt", "record.txt"}) {
            std::error_code ec;
            fs::remove(config / "openhd" / role, ec);
            if (ec) { error = "Could not replace factory role marker"; return false; }
        }
        break;
    }
    for (const auto& file : files) {
        if (!atomic_write(config / fs::u8path(file.path), file.data)) {
            error = "Could not persist provisioning file " + file.path; return false;
        }
    }
    // Marker is last. A failed or interrupted import is retried on next boot;
    // a completed import never reinstates settings consumed by SysUtils.
    if (!atomic_write(marker, identity)) { error = "Could not persist provisioning marker"; return false; }
    return true;
}

bool sysutil::import_ubi_provisioning() {
    std::error_code ec;
    if (!fs::is_directory("/sys/class/ubi", ec)) return true;
    fs::path selected;
    for (const auto& entry : fs::directory_iterator("/sys/class/ubi", ec)) {
        const auto name = entry.path().filename().string();
        const auto separator = name.find('_');
        if (name.rfind("ubi", 0) != 0 || separator == std::string::npos ||
            read_text(entry.path() / "name") != "openhd-provision") continue;
        const auto device = name.substr(0, separator);
        const auto mtd = read_text(fs::path("/sys/class/ubi") / device / "mtd_num");
        if (mtd.empty() || mtd.find_first_not_of("0123456789") != std::string::npos ||
            read_text(fs::path("/sys/class/mtd") / ("mtd" + mtd) / "name") != "ohd" ||
            read_text(entry.path() / "type") != "static") continue;
        if (!selected.empty()) { std::cerr << "[provisioning] Multiple OHD provisioning volumes\n"; return false; }
        selected = fs::path("/dev") / name;
    }
    if (selected.empty()) return true;
    std::ifstream input(selected, std::ios::binary);
    std::string capsule(16, '\0');
    if (!input.read(&capsule[0], 16)) return false;
    const auto size = openhd_provisioning::get32(capsule.data() + 8);
    if (size < 4 || size > openhd_provisioning::max_size - 16) return false;
    capsule.resize(16 + size);
    if (!input.read(&capsule[16], size)) return false;
    std::string error;
    if (!apply_provisioning(capsule, "/Config", error)) {
        std::cerr << "[provisioning] " << error << '\n'; return false;
    }
    return true;
}
