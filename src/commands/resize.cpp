#include "resize.h"

#include "utils/filesystem_utils.h"
#include "utils/process.h"
#include "utils/string_utils.h"

#include <cctype>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

bool runResize(const std::string &partitionUuid, const std::string &partitionNumber,
               bool forceResizeRequest) {
    fs::path markerA{"/boot/openhd/openhd/resize.txt"};
    fs::path markerB{"/boot/openhd/resize.txt"};
    bool requested = forceResizeRequest || fs::exists(markerA) || fs::exists(markerB);
    if (!requested) {
        std::cout << "Resize not requested. The file /boot/openhd/resize.txt does not exist." << std::endl;
        return false;
    }

    auto blkidResult = runProcess({"blkid", "-l", "-o", "device", "-t", "UUID=" + partitionUuid});
    std::string devicePath = trim(blkidResult.output);
    if (!blkidResult.success || devicePath.empty()) {
        std::cerr << "Partition with UUID " << partitionUuid << " not found." << std::endl;
        return false;
    }

    std::string mountPoint = devicePath;
    while (!mountPoint.empty() && std::isdigit(mountPoint.back())) {
        mountPoint.pop_back();
    }

    std::ostringstream fdiskInput;
    fdiskInput << "d\n" << partitionNumber << "\n"
               << "n\n" << partitionNumber << "\n\n\nw\n";

    std::cout << "Resizing partition " << devicePath << " (uuid " << partitionUuid << ")" << std::endl;
    auto fdiskResult = runProcess({"fdisk", mountPoint}, fdiskInput.str());
    if (!fdiskResult.success) {
        std::cerr << "fdisk failed for " << mountPoint << std::endl;
        return false;
    }

    auto partprobeResult = runProcess({"partprobe", devicePath});
    if (!partprobeResult.success) {
        std::cerr << "partprobe failed for " << devicePath << std::endl;
        return false;
    }

    auto resizeResult = runProcess({"resize2fs", "/dev/disk/by-uuid/" + partitionUuid});
    if (!resizeResult.success) {
        std::cerr << "resize2fs failed for uuid " << partitionUuid << std::endl;
        return false;
    }

    if (fs::exists(markerA)) {
        fs::remove(markerA);
    }
    if (fs::exists(markerB)) {
        fs::remove(markerB);
    }

    runProcess({"reboot"});
    return true;
}
