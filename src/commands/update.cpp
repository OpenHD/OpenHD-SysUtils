#include "update.h"

#include "utils/filesystem_utils.h"
#include "utils/process.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

void handleUpdate() {
    const fs::path updateFolder{"/boot/openhd/update"};
    const fs::path tempFolder{"/tmp/updateOpenHD"};
    const fs::path logFile{"/boot/openhd/install-log.txt"};

    ensureDirectory(tempFolder);

    if (!fs::exists(updateFolder)) {
        std::cerr << "Error: " << updateFolder << " does not exist" << std::endl;
        return;
    }

    fs::path updateZip = updateFolder / "update.zip";
    if (fs::exists(updateZip)) {
        runProcess({"unzip", updateZip.string(), "-d", tempFolder.string()});
        fs::remove(updateZip);
    }

    std::ofstream(logFile).close();
    bool allSuccessful = true;
    bool foundDeb = false;

    for (const auto &entry : fs::directory_iterator(tempFolder)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".deb") {
            continue;
        }
        foundDeb = true;
        std::cout << "Installing " << entry.path() << std::endl;
        auto result = runProcess({"dpkg", "-i", "--force-overwrite", entry.path().string()}, {}, logFile, true);
        std::ofstream logStream(logFile, std::ios::app);
        if (result.success) {
            logStream << "Success: " << entry.path() << " installed successfully" << std::endl;
        } else {
            logStream << "Failure: Failed to install " << entry.path() << std::endl;
            allSuccessful = false;
        }
    }

    if (foundDeb && allSuccessful) {
        std::cout << "All .deb files were installed successfully, rebooting the system" << std::endl;
        fs::remove_all(updateFolder);
        fs::remove_all(tempFolder);
        runProcess({"reboot"});
    } else if (!foundDeb) {
        std::cout << "No .deb files found in " << tempFolder << std::endl;
    } else {
        runProcess({"wall", "The update has failed, please do a manual flash"});
    }
}
