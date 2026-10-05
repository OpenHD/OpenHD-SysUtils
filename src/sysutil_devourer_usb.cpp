#include "sysutil_devourer_usb.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace sysutil {
namespace {
namespace fs = std::filesystem;

std::string read_token(const fs::path& path) {
  std::ifstream input(path);
  std::string value;
  input >> value;
  return value;
}

bool unmanage_interface(const std::string& interface) {
  // Systems without NetworkManager need no notification. Avoid shell expansion
  // of interface names and bound the wait so Wi-Fi setup cannot stall startup.
  const char* binary = nullptr;
  for (const char* path : {"/usr/bin/nmcli", "/bin/nmcli"}) {
    if (::access(path, X_OK) == 0) {
      binary = path;
      break;
    }
  }
  if (!binary) return true;
  const pid_t child = ::fork();
  if (child == 0) {
    ::execl(binary, binary, "--wait", "2", "device", "set", interface.c_str(),
            "managed", "no", static_cast<char*>(nullptr));
    _exit(127);
  }
  if (child < 0) return false;
  int status = 0;
  for (int tick = 0; tick < 30; ++tick) {
    const auto result = ::waitpid(child, &status, WNOHANG);
    if (result == child) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (result < 0 && errno != EINTR) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  ::kill(child, SIGKILL);
  while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
  return false;
}
}  // namespace

unsigned prepare_devourer_eu_usb(
    const fs::path& usb_root,
    const std::function<bool(const std::string&)>& set_unmanaged) {
#ifndef OPENHD_DEVOURER_RTL8812EU
  return 0;
#else
  unsigned released = 0;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(usb_root, ec)) {
    // USB interface symlinks point into their parent USB device in sysfs.
    if (entry.path().filename().string().find(':') == std::string::npos) continue;
    const auto interface_path = fs::canonical(entry.path(), ec);
    if (ec) { ec.clear(); continue; }
    const auto device = interface_path.parent_path();
    const auto product = read_token(device / "idProduct");
    if (read_token(device / "idVendor") != "0bda" ||
        (product != "a81a" && product != "a82a" && product != "e822")) continue;
    const auto driver = fs::canonical(interface_path / "driver", ec);
    if (ec) { ec.clear(); continue; }
    const auto name = driver.filename().string();
    // In particular, leave an active Devourer usbfs claim alone.
    if (name != "rtl88x2eu_ohd" && name != "88x2eu_ohd") continue;
    bool unmanaged = true;
    std::error_code net_ec;
    for (const auto& net : fs::directory_iterator(interface_path / "net", net_ec)) {
      const auto iface = net.path().filename().string();
      if (!(set_unmanaged ? set_unmanaged(iface) : unmanage_interface(iface))) {
        std::cerr << "[sysutils][wifi] Cannot release EU interface " << iface
                  << ": NetworkManager ownership was not cleared.\n";
        unmanaged = false;
      }
    }
    if (net_ec && net_ec != std::errc::no_such_file_or_directory) unmanaged = false;
    if (!unmanaged) continue;
    std::ofstream unbind(driver / "unbind");
    unbind << interface_path.filename().string();
    unbind.close();
    if (!unbind) {
      std::cerr << "[sysutils][wifi] Failed to release EU kernel interface "
                << interface_path.filename().string() << ".\n";
      continue;
    }
    ++released;
    std::cerr << "[sysutils][wifi] Released EU kernel interface "
              << interface_path.filename().string() << " for Devourer.\n";
  }
  return released;
#endif
}
}  // namespace sysutil
