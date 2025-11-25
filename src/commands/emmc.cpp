#include "emmc.h"

#include "utils/filesystem_utils.h"
#include "utils/process.h"
#include "utils/string_utils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {
struct EmmcContext {
    std::string board;
    std::string emmc;
    std::string sdcard;
};

EmmcContext detectBoard() {
    EmmcContext ctx;
    std::string model = readTextFile("/proc/device-tree/model");
    model.erase(std::remove(model.begin(), model.end(), '\0'), model.end());
    ctx.board = trim(model);

    if (ctx.board == "Radxa CM3 RPI CM4 IO") {
        ctx.emmc = "/dev/mmcblk0";
        ctx.sdcard = "/dev/mmcblk1";
    } else if (ctx.board == "Radxa ZERO 3") {
        ctx.emmc = "/dev/mmcblk0";
        ctx.sdcard = "/dev/mmcblk1";
    } else if (ctx.board == "Radxa ROCK 5B") {
        ctx.emmc = "/dev/mmcblk3";
        ctx.sdcard = "/dev/mmcblk4";
    } else if (ctx.board == "Radxa ROCK 5A") {
        ctx.emmc = "/dev/mmcblk4";
        ctx.sdcard = "/dev/mmcblk9";
    } else if (ctx.board == "CM5 RPI CM4 IO") {
        ctx.emmc = "/dev/mmcblk4";
        ctx.sdcard = "/dev/mmcblk2";
    } else if (ctx.board == "OpenHD X20 Dev") {
        ctx.emmc = "/dev/mmcblk1";
        ctx.sdcard = "/dev/mmcblk0";
    }
    return ctx;
}

void debugMessage(const std::string &message, bool debugEnabled) {
    std::cout << message << std::endl;
    if (!debugEnabled) {
        return;
    }
    fs::path logPath{"/boot/openhd/emmc_tool.log"};
    ensureDirectory(logPath.parent_path());
    std::ofstream log(logPath, std::ios::app);
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    log << std::put_time(std::localtime(&now), "%F %T") << ' ' << message << std::endl;
}

bool copyImageToDevice(const fs::path &image, const std::string &device, bool debugEnabled) {
    std::ifstream source(image, std::ios::binary);
    std::ofstream target(device, std::ios::binary);
    if (!source.is_open() || !target.is_open()) {
        debugMessage("Failed to open image or device", debugEnabled);
        return false;
    }

    constexpr std::size_t bufferSize = 8 * 1024 * 1024;
    std::vector<char> buffer(bufferSize);
    std::uintmax_t totalBytes = fs::file_size(image);
    std::uintmax_t written = 0;

    while (source) {
        source.read(buffer.data(), buffer.size());
        std::streamsize readBytes = source.gcount();
        if (readBytes <= 0) {
            break;
        }
        target.write(buffer.data(), readBytes);
        written += static_cast<std::uintmax_t>(readBytes);
        int percent = static_cast<int>((written * 100) / (totalBytes ? totalBytes : 1));
        std::cout << "Flashing progress: " << percent << "%\r" << std::flush;
    }
    std::cout << std::endl;
    target.flush();
    return source.eof() && target.good();
}
}

void handleEmmc(const std::string &command, bool debugEnabled) {
    EmmcContext ctx = detectBoard();
    if (ctx.emmc.empty()) {
        std::cerr << "Unsupported board: " << ctx.board << std::endl;
        return;
    }

    debugMessage("EMMC: " + ctx.emmc, debugEnabled);
    debugMessage("SDCARD: " + ctx.sdcard, debugEnabled);

    runProcess({"/usr/local/bin/led_sys.sh", "off"});

    if (command == "clear") {
        runProcess({"/usr/local/bin/led_sys.sh", "flashing", "blueANDgreen", "2"});
        runProcess({"sudo", "dd", "if=/dev/zero", "of=" + ctx.emmc, "bs=512", "count=1", "seek=1"});
        runProcess({"/usr/local/bin/led_sys.sh", "off"});
        return;
    }

    if (command == "flash") {
        runProcess({"/usr/local/bin/led_sys.sh", "flashing", "blueANDgreen", "2"});
        fs::path imagePath{"/opt/additionalFiles/emmc.img"};
        if (!fs::exists(imagePath)) {
            debugMessage("Failed emmc.img not found", debugEnabled);
            runProcess({"/usr/local/bin/led_sys.sh", "off"});
            return;
        }

        debugMessage(std::to_string(fs::file_size(imagePath)) + " image is being flashed!", debugEnabled);
        if (!copyImageToDevice(imagePath, ctx.emmc, debugEnabled)) {
            debugMessage("Flashing failed", debugEnabled);
            runProcess({"/usr/local/bin/led_sys.sh", "off"});
            return;
        }

        ensureDirectory("/media/new");
        runProcess({"mount", ctx.emmc + "p1", "/media/new"});
        if (fs::exists("/boot/openhd")) {
            ensureDirectory("/media/new/openhd");
            fs::copy("/boot/openhd", "/media/new/openhd",
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }
        debugMessage("Copied openhd config files!", debugEnabled);
        runProcess({"/usr/local/bin/led_sys.sh", "off"});
        fs::remove_all("/etc/profile");
        runProcess({"reboot"});
        return;
    }

    std::cerr << "Unsupported command" << std::endl;
}
