#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string trim(std::string value) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool ensureDirectory(const fs::path &path) {
    std::error_code ec;
    if (fs::exists(path, ec)) {
        return fs::is_directory(path, ec);
    }
    return fs::create_directories(path, ec);
}

struct ProcessResult {
    bool success{false};
    int exitCode{0};
    std::string output;
};

ProcessResult runProcess(const std::vector<std::string> &args,
                         const std::string &input = {},
                         const std::optional<fs::path> &redirectStdout = std::nullopt,
                         bool mergeStderr = true) {
    ProcessResult result;
    if (args.empty()) {
        return result;
    }

    int inputPipe[2]{-1, -1};
    if (!input.empty() && pipe(inputPipe) == -1) {
        perror("pipe");
        return result;
    }

    int outputPipe[2]{-1, -1};
    bool captureOutput = !redirectStdout.has_value();
    if (captureOutput && pipe(outputPipe) == -1) {
        perror("pipe");
        if (inputPipe[0] != -1) {
            close(inputPipe[0]);
            close(inputPipe[1]);
        }
        return result;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        if (inputPipe[0] != -1) {
            close(inputPipe[0]);
            close(inputPipe[1]);
        }
        if (outputPipe[0] != -1) {
            close(outputPipe[0]);
            close(outputPipe[1]);
        }
        return result;
    }

    if (pid == 0) {
        // Child
        if (!input.empty()) {
            close(inputPipe[1]);
            dup2(inputPipe[0], STDIN_FILENO);
            close(inputPipe[0]);
        }

        if (redirectStdout.has_value()) {
            int fd = ::open(redirectStdout->c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
            if (fd >= 0) {
                dup2(fd, STDOUT_FILENO);
                if (mergeStderr) {
                    dup2(fd, STDERR_FILENO);
                }
                close(fd);
            }
        } else {
            close(outputPipe[0]);
            dup2(outputPipe[1], STDOUT_FILENO);
            if (mergeStderr) {
                dup2(outputPipe[1], STDERR_FILENO);
            }
            close(outputPipe[1]);
        }

        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const auto &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        std::perror("execvp");
        _exit(127);
    }

    // Parent
    if (!input.empty()) {
        close(inputPipe[0]);
        ssize_t written = write(inputPipe[1], input.data(), input.size());
        (void)written;
        close(inputPipe[1]);
    }

    if (captureOutput) {
        close(outputPipe[1]);
        std::array<char, 4096> buffer{};
        ssize_t count;
        while ((count = read(outputPipe[0], buffer.data(), buffer.size())) > 0) {
            result.output.append(buffer.data(), static_cast<size_t>(count));
        }
        close(outputPipe[0]);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
        result.success = result.exitCode == 0;
    }
    return result;
}

std::string readTextFile(const fs::path &path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void printBanner() {
    std::cout << " #######  ########  ######## ##    ## ##     ## ########\n"
              << "##     ## ##     ## ##       ###   ## ##     ## ##     ##\n"
              << "##     ## ##     ## ##       ####  ## ##     ## ##     ##\n"
              << "##     ## ########  ######   ## ## ## ######### ##     ##\n"
              << "##     ## ##        ##       ##  #### ##     ## ##     ##\n"
              << "##     ## ##        ##       ##   ### ##     ## ##     ##\n"
              << " #######  ##        ######## ##    ## ##     ## ########\n"
              << "----------------------- SysUtils  -----------------------\n\n"
              << "Started!\n\n";
}

bool moveFile(const fs::path &from, const fs::path &toDirectory) {
    std::error_code ec;
    fs::create_directories(toDirectory, ec);
    fs::path destination = toDirectory / from.filename();
    fs::rename(from, destination, ec);
    if (ec) {
        std::cerr << "Failed to move " << from << " to " << destination << ": " << ec.message() << "\n";
        return false;
    }
    return true;
}

bool runResize(const std::string &partitionUuid, const std::string &partitionNumber,
               bool forceResizeRequest = false) {
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

    auto handleRock = [](const fs::path &marker) {
        if (fs::exists(marker)) {
            runProcess({"sudo", "/usr/local/bin/initRock.sh"});
            fs::remove(marker);
        }
    };

    handleRock("/boot/openhd/rock-5a.txt");
    handleRock("/boot/openhd/rock-5b.txt");

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
            fs::copy("/boot/openhd", "/media/new/openhd", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }
        debugMessage("Copied openhd config files!", debugEnabled);
        runProcess({"/usr/local/bin/led_sys.sh", "off"});
        fs::remove_all("/etc/profile");
        runProcess({"reboot"});
        return;
    }

    std::cerr << "Unsupported command" << std::endl;
}

void printUsage(const std::string &name) {
    std::cout << "Usage: " << name << " <command> [args]\n"
              << "Commands:\n"
              << "  init                     Run boot-time initialization logic\n"
              << "  update                   Install .deb updates from /boot/openhd/update\n"
              << "  resize <uuid> <partnr>   Resize a partition by UUID and partition number\n"
              << "  emmc <clear|flash> [debug]  Manage eMMC operations\n";
}

} // namespace

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
