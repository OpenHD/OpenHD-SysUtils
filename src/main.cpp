#include "commands/emmc.h"
#include "commands/init.h"
#include "commands/resize.h"
#include "commands/update.h"
#include "ui/banner.h"

#include <iostream>
#include <string>
#include <string_view>

namespace {
void printUsage(const std::string &name) {
    std::cout << "Usage: " << name << " <command> [args]\n"
              << "Commands:\n"
              << "  init                     Run boot-time initialization logic\n"
              << "  update                   Install .deb updates from /boot/openhd/update\n"
              << "  resize <uuid> <partnr>   Resize a partition by UUID and partition number\n"
              << "  emmc <clear|flash> [debug]  Manage eMMC operations\n";
}
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];
    if (command == "init") {
        handleInit();
    } else if (command == "update") {
        handleUpdate();
    } else if (command == "resize") {
        if (argc < 4) {
            std::cerr << "resize requires a partition UUID and a partition number" << std::endl;
            return 1;
        }
        runResize(argv[2], argv[3]);
    } else if (command == "emmc") {
        if (argc < 3) {
            std::cerr << "emmc requires a command (clear|flash)" << std::endl;
            return 1;
        }
        bool debugEnabled = argc >= 4 && std::string_view(argv[3]) == "debug";
        handleEmmc(argv[2], debugEnabled);
    } else {
        printUsage(argv[0]);
        return 1;
    }
    return 0;
}
