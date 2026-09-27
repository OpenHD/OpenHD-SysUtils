#include "sysutil_usb_modeswitch.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace sysutil {
namespace {

namespace fs = std::filesystem;
constexpr const char* kUsbRoot = "/sys/bus/usb/devices";
// The usb-modeswitch package already starts an eject from udev on device add.
// Give that first attempt time to finish before SysUtils retries the device.
constexpr auto kUdevGrace = std::chrono::seconds(30);
// Some 0bda:1a2b adapters accept the eject only on a subsequent attempt.
// Wi-Fi discovery retries every five seconds while no compatible card exists.
constexpr auto kRetryInterval = std::chrono::seconds(5);
constexpr int kMaxAttemptsPerPort = 3;

std::string read_trimmed(const fs::path& path) {
  std::ifstream input(path);
  std::string value;
  input >> value;
  return value;
}

bool has_storage_interface(const fs::path& device) {
  std::error_code ec;
  const auto prefix = device.filename().string() + ":";
  for (const auto& entry : fs::directory_iterator(kUsbRoot, ec)) {
    if (entry.path().filename().string().rfind(prefix, 0) == 0 &&
        read_trimmed(entry.path() / "bInterfaceClass") == "08") return true;
  }
  return false;
}

const char* modeswitch_binary() {
  for (const char* candidate : { "/usr/sbin/usb_modeswitch", "/sbin/usb_modeswitch",
                                  "/usr/bin/usb_modeswitch" }) {
    if (::access(candidate, X_OK) == 0) return candidate;
  }
  return nullptr;
}

bool run_eject(const char* binary, const std::string& bus,
               const std::string& device_number) {
  const pid_t child = ::fork();
  if (child == 0) {
    ::execl(binary, binary, "-K", "-v", "0x0bda", "-p", "0x1a2b",
            "-b", bus.c_str(), "-g", device_number.c_str(),
            static_cast<char*>(nullptr));
    _exit(127);
  }
  if (child < 0) return false;
  int status = 0;
  for (int tick = 0; tick < 100; ++tick) {
    if (::waitpid(child, &status, WNOHANG) == child)
      return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  ::kill(child, SIGKILL);
  ::waitpid(child, &status, 0);
  return false;
}

}  // namespace

bool switch_realtek_zerocd_wifi() {
  std::error_code ec;
  if (!fs::exists(kUsbRoot, ec)) return false;
  const char* binary = modeswitch_binary();
  static std::map<std::string, std::pair<int, std::chrono::steady_clock::time_point>> attempts;
  static std::map<std::string, std::chrono::steady_clock::time_point> first_seen;
  bool switched = false;
  for (const auto& entry : fs::directory_iterator(kUsbRoot, ec)) {
    const auto device = entry.path();
    if (read_trimmed(device / "idVendor") != "0bda" ||
        read_trimmed(device / "idProduct") != "1a2b" ||
        !has_storage_interface(device)) continue;

    const auto port = device.filename().string();
    const std::string bus = read_trimmed(device / "busnum");
    const std::string devnum = read_trimmed(device / "devnum");
    if (bus.empty() || devnum.empty()) continue;
    // A replug gets a new device number and must have its own retry budget.
    const auto instance = port + ":" + bus + ":" + devnum;
    const auto now = std::chrono::steady_clock::now();
    const auto seen = first_seen.emplace(instance, now).first;
    if (now - seen->second < kUdevGrace) continue;
    auto& attempt = attempts[instance];
    if (attempt.first >= kMaxAttemptsPerPort ||
        (attempt.first > 0 && now - attempt.second < kRetryInterval)) continue;
    if (!binary) {
      std::cerr << "[sysutils][wifi] 0bda:1a2b ZeroCD adapter at " << port
                << " needs usb_modeswitch, but the executable is missing.\n";
      continue;
    }
    ++attempt.first;
    attempt.second = now;
    std::cerr << "[sysutils][wifi] Ejecting Realtek ZeroCD adapter 0bda:1a2b at "
              << port << " (attempt " << attempt.first << ").\n";
    const bool command_ok = run_eject(binary, bus, devnum);
    bool switched_this_device = false;
    for (int tick = 0; tick < 50; ++tick) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      const auto product = read_trimmed(device / "idProduct");
      if (product != "1a2b") {
        std::cerr << "[sysutils][wifi] Realtek adapter at " << port
                  << " left ZeroCD mode; new USB product "
                  << (product.empty() ? "pending" : product) << ".\n";
        switched = true;
        switched_this_device = true;
        break;
      }
    }
    if (!switched_this_device) {
      std::cerr << "[sysutils][wifi] ZeroCD mode persists at " << port
                << " after usb_modeswitch" << (command_ok ? "" : " (command failed)")
                << ".\n";
    }
  }
  return switched;
}

}  // namespace sysutil
