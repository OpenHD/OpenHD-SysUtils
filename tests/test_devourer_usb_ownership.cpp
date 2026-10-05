#include "sysutil_devourer_usb.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <unistd.h>

namespace fs = std::filesystem;
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void write(const fs::path& path, const std::string& value) {
  fs::create_directories(path.parent_path());
  std::ofstream(path) << value;
}
std::string read(const fs::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input), {});
}

int main() {
  const auto root = fs::temp_directory_path() /
                    ("openhd-eu-ownership-test-" + std::to_string(::getpid()));
  fs::remove_all(root);
  fs::create_directories(root / "usb");
  const auto add = [&](const std::string& port, const std::string& vendor,
                       const std::string& product, const std::string& driver) {
    const auto device = root / "devices" / port;
    const auto interface = device / (port + ":1.0");
    write(device / "idVendor", vendor);
    write(device / "idProduct", product);
    fs::create_directories(interface / "net" / ("wlan-" + port));
    fs::create_directory_symlink(device, root / "usb" / port);
    fs::create_directory_symlink(interface, root / "usb" / (port + ":1.0"));
    const auto driver_path = root / "drivers" / port / driver;
    write(driver_path / "unbind", "unchanged");
    fs::create_directory_symlink(driver_path, interface / "driver");
    return driver_path / "unbind";
  };
  try {
    const auto eu = add("1-1.2", "0bda", "a81a", "rtl88x2eu_ohd");
    const auto eu2 = add("1-1.3", "0bda", "a82a", "88x2eu_ohd");
    const auto eu3 = add("1-1.4", "0bda", "e822", "rtl88x2eu_ohd");
    const auto active = add("1-1.5", "0bda", "a81a", "usbfs");
    const auto bu = add("1-1.6", "0bda", "b812", "rtl88x2bu_ohd");
    const auto cu = add("1-1.7", "0bda", "c812", "rtl88x2cu_ohd");
    const auto zerocd = add("1-1.8", "0bda", "1a2b", "usb-storage");
    const auto other = add("1-1.9", "1234", "a81a", "rtl88x2eu_ohd");
    unsigned notified = 0;
    const auto released = sysutil::prepare_devourer_eu_usb(root / "usb",
        [&](const std::string& iface) {
          ++notified;
          // Check that NM ownership is cleared before unbinding.
          if (iface == "wlan-1-1.2") require(read(eu) == "unchanged", "unbind ordering");
          return true;
        });
#ifdef OPENHD_DEVOURER_RTL8812EU
    require(released == 3 && notified == 3, "EU identities must be released");
    require(read(eu) == "1-1.2:1.0" && read(eu2) == "1-1.3:1.0" &&
            read(eu3) == "1-1.4:1.0", "USB interface name must be written");
    write(eu, "unchanged");
    require(sysutil::prepare_devourer_eu_usb(root / "usb",
        [](const std::string&) { return false; }) == 0, "NM failure must retain driver");
    require(read(eu) == "unchanged", "failed notification must not unbind");
    // A removal racing startup must not release another adapter or throw.
    fs::remove_all(root / "devices" / "1-1.2");
    require(sysutil::prepare_devourer_eu_usb(root / "usb",
        [](const std::string&) { return true; }) == 2, "vanished EU must be skipped");
#else
    require(released == 0 && notified == 0, "kernel-only build must not take ownership");
    require(read(eu) == "unchanged", "kernel-only build must retain EU driver");
#endif
    for (const auto& path : {active, bu, cu, zerocd, other}) {
      require(read(path) == "unchanged", "unrelated or active USB device was modified");
    }
    require(sysutil::prepare_devourer_eu_usb(root / "missing") == 0,
            "missing sysfs must be harmless");
    fs::remove_all(root);
    std::cout << "USB ownership tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    fs::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
