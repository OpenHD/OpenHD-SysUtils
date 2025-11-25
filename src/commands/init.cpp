#include "init.h"

#include "commands/resize.h"
#include "ui/banner.h"
#include "utils/filesystem_utils.h"
#include "utils/process.h"
#include "utils/string_utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {
bool handleRockBoard(const fs::path &marker) {
    if (!fs::exists(marker)) {
        return false;
    }
    runProcess({"sudo", "/usr/local/bin/initRock.sh"});
    fs::remove(marker);
    return true;
}
}

void handleInit() {
    printBanner();

    fs::path debugFile{"/boot/openhd/debug.txt"};
    if (fs::exists(debugFile)) {
        std::cout << "debug mode selected" << std::endl;
        moveFile(debugFile, "/usr/local/share/openhd");
    }

    fs::path spaceImg{"/opt/space.img"};
    if (fs::exists(spaceImg)) {
        fs::remove(spaceImg);
    }

    if (fs::exists("/external/openhd/hardware_vtx_v20.txt")) {
        runProcess({"sudo", "/usr/local/bin/initX20.sh"});
    }

    if (fs::exists("/boot/openhd/openhd/x86.txt")) {
        for (const auto &entry : fs::directory_iterator("/boot/openhd/openhd")) {
            moveFile(entry.path(), "/boot/openhd");
        }
    }

    auto lsb = runProcess({"lsb_release", "-cs"});
    std::string codename = trim(lsb.output);
    if (codename == "noble" && fs::exists("/opt/setup")) {
        runProcess({"depmod", "-a"});
        fs::remove("/opt/setup");
        runResize("404f7966-7c54-4170-8523-ed6a2a8da9bd", "3", true);
        runProcess({"reboot"});
        return;
    }

    if (fs::exists("/boot/openhd/x86.txt")) {
        runProcess({"sudo", "/usr/local/bin/initX86.sh"});
        ensureDirectory("/usr/local/share");
        std::ofstream("/usr/local/share/executed").close();
        fs::remove("/boot/openhd/x86.txt");
    }

    handleRockBoard("/boot/openhd/rock-5a.txt");
    handleRockBoard("/boot/openhd/rock-5b.txt");

    if (fs::exists("/config/openhd/rock-rk3566.txt")) {
        std::cout << "detected rk3566 device" << std::endl;
        runProcess({"sudo", "/usr/local/bin/initRock.sh"});
        if (fs::exists("/config/openhd/clearEMMC.txt")) {
            runProcess({"/usr/local/bin/openhd_sys_utils", "emmc", "clear"});
            runProcess({"whiptail", "--msgbox", "EMMC cleared Please reboot your system now", "10", "40"});
        }
    }

    if (fs::exists("/boot/openhd/rpi.txt")) {
        runProcess({"sudo", "/usr/local/bin/initPi.sh"});
        fs::remove("/boot/openhd/rpi.txt");
    }
}
