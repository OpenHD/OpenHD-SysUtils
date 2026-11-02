#include "sysutil_usb_power.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <linux/usbdevice_fs.h>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace sysutil {
namespace {
namespace fs = std::filesystem;
std::string read(const fs::path &path) {
  std::ifstream input(path);
  std::string value;
  input >> value;
  return value;
}
bool is_usb3(const fs::path &hub) {
  const auto version = read(hub / "version");
  return !version.empty() && version.front() >= '3' && version.front() <= '9';
}
std::string bus_node(const fs::path &hub) {
  const auto bus = read(hub / "busnum"), device = read(hub / "devnum");
  if (!std::regex_match(bus, std::regex("[0-9]{1,3}")) ||
      !std::regex_match(device, std::regex("[0-9]{1,3}")))
    return {};
  return "/dev/bus/usb/" + std::string(3 - bus.size(), '0') + bus + "/" +
         std::string(3 - device.size(), '0') + device;
}
bool control(int fd, unsigned char request_type, unsigned char request,
             unsigned short value, unsigned short index, void *data = nullptr,
             unsigned short length = 0) {
  usbdevfs_ctrltransfer transfer{};
  transfer.bRequestType = request_type;
  transfer.bRequest = request;
  transfer.wValue = value;
  transfer.wIndex = index;
  transfer.wLength = length;
  transfer.timeout = 1500;
  transfer.data = data;
  return ioctl(fd, USBDEVFS_CONTROL, &transfer) == length;
}
std::optional<UsbHubPowerInfo> hub_info(const fs::path &hub) {
  const auto node = bus_node(hub);
  if (node.empty())
    return {};
  const int fd = open(node.c_str(), O_RDWR | O_CLOEXEC);
  if (fd < 0)
    return {};
  unsigned char descriptor[12]{};
  const bool usb3 = is_usb3(hub);
  const bool ok = control(fd, 0xa0, 6, (usb3 ? 0x2a : 0x29) << 8, 0, descriptor,
                          usb3 ? 12 : 7);
  close(fd);
  if (!ok || !descriptor[2])
    return {};
  const auto mode = descriptor[3] & 3;
  return UsbHubPowerInfo{descriptor[2], mode == 1, mode < 2};
}
bool descendant(const std::string &device, const std::string &hub,
                unsigned port) {
  const auto prefix = hub.rfind("usb", 0) == 0
                          ? hub.substr(3) + "-" + std::to_string(port)
                          : hub + "." + std::to_string(port);
  return device == prefix || device.rfind(prefix + ".", 0) == 0;
}
} // namespace

std::vector<UsbHubPowerTarget>
plan_usb_wifi_power_cycle(const fs::path &root, const std::string &device,
                          bool pi4, const UsbHubQuery &query) {
  if (!std::regex_match(device, std::regex("[0-9]+-[0-9]+(\\.[0-9]+)*")))
    return {};
  const auto split = device.find_last_of("-.");
  const auto hub = split == device.find('-') ? "usb" + device.substr(0, split)
                                             : device.substr(0, split);
  const unsigned port = std::stoul(device.substr(split + 1));
  const auto info = query(root / hub);
  if (!info || !info->switchable || !port || port > info->ports)
    return {};
  // Early Pi 4 boards advertise individual switches but share the VBUS rail
  // across the internal USB2 hub and the companion USB3 root hub.
  const bool shared = pi4 && read(root / hub / "idVendor") == "2109" &&
                      read(root / hub / "idProduct") == "3431";
  std::vector<UsbHubPowerTarget> result;
  const auto add = [&](const fs::path &path, const UsbHubPowerInfo &details,
                       bool all) {
    UsbHubPowerTarget target{path, {}};
    if (all || !details.individual)
      for (unsigned p = 1; p <= details.ports; ++p)
        target.ports.push_back(p);
    else
      target.ports.push_back(port);
    result.push_back(std::move(target));
  };
  add(root / hub, *info, shared);
  std::error_code ec;
  if (shared) {
    const auto controller =
        fs::canonical(root / ("usb" + device.substr(0, device.find('-'))), ec)
            .parent_path();
    if (ec)
      return {};
    bool companion = false;
    for (const auto &entry : fs::directory_iterator(root, ec)) {
      const auto name = entry.path().filename().string();
      if (!std::regex_match(name, std::regex("usb[0-9]+")) ||
          !is_usb3(entry.path()))
        continue;
      const auto parent = fs::canonical(entry.path(), ec).parent_path();
      if (ec || parent != controller)
        continue;
      const auto other = query(entry.path());
      if (!other || !other->switchable)
        return {};
      add(entry.path(), *other, true);
      companion = true;
    }
    if (!companion)
      return {};
  }
  // Never cut a USB disk, including USB-root filesystems. The Realtek virtual
  // driver disk itself is the sole storage-class exception.
  for (const auto &entry : fs::directory_iterator(root, ec)) {
    if (read(entry.path() / "bInterfaceClass") != "08")
      continue;
    const auto name = entry.path().filename().string();
    const auto device_name = name.substr(0, name.find(':'));
    if (read(root / device_name / "idVendor") == "0bda" &&
        read(root / device_name / "idProduct") == "1a2b")
      continue;
    for (const auto &target : result)
      for (auto p : target.ports)
        if (descendant(device_name, target.hub.filename().string(), p))
          return {};
  }
  if (ec)
    return {};
  return result;
}

bool cycle_usb_power(
    const std::vector<UsbHubPowerTarget> &targets,
    const std::function<bool(const fs::path &, unsigned, bool)> &power,
    const std::function<void()> &wait) {
  std::vector<std::pair<fs::path, unsigned>> touched;
  const auto restore = [&] {
    bool ok = true;
    for (auto i = touched.rbegin(); i != touched.rend(); ++i) {
      bool restored = false;
      for (int retry = 0; retry < 3 && !restored; ++retry)
        restored = power(i->first, i->second, true);
      ok = restored && ok;
    }
    return ok;
  };
  if (targets.empty())
    return false;
  try {
    for (const auto &target : targets)
      for (auto port : target.ports) {
        touched.emplace_back(target.hub, port);
        if (!power(target.hub, port, false)) {
          restore();
          return false;
        }
      }
    wait();
  } catch (...) {
    restore();
    throw;
  }
  return restore();
}

bool perform_zerocd_usb_power_recovery(const std::string &port,
                                       bool check_only) {
  const fs::path root = "/sys/bus/usb/devices";
  if (read(root / port / "idVendor") != "0bda" ||
      (!check_only && read(root / port / "idProduct") != "1a2b"))
    return false;
  std::ifstream model_file("/proc/device-tree/model");
  std::string model;
  std::getline(model_file, model, '\0');
  const auto plan = plan_usb_wifi_power_cycle(
      root, port, model.rfind("Raspberry Pi 4 Model B", 0) == 0, hub_info);
  if (plan.empty()) {
    std::cerr << "[sysutils][wifi] USB power recovery unavailable at " << port
              << ": unsupported hub or USB storage shares its power rail.\n";
    return false;
  }
  if (check_only) {
    for (const auto &target : plan) {
      std::cout << target.hub.filename().string() << " ports:";
      for (auto p : target.ports)
        std::cout << ' ' << p;
      std::cout << '\n';
    }
    return true;
  }
  // Keep hub handles open before cutting any downstream power.
  struct Handles {
    std::map<fs::path, int> fds;
    ~Handles() {
      for (auto &item : fds)
        close(item.second);
    }
  } handles;
  for (const auto &target : plan) {
    const int fd = open(bus_node(target.hub).c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0)
      return false;
    handles.fds.emplace(target.hub, fd);
  }
  std::error_code ec;
  fs::create_directories("/run/openhd/usb-power-recovery", ec);
  if (ec || !fs::create_directory(
                fs::path("/run/openhd/usb-power-recovery") / port, ec))
    return false;
  std::cerr
      << "[sysutils][wifi] Power-cycling persistent Realtek ZeroCD adapter at "
      << port
      << " once this boot (shared hubs may briefly disconnect other USB "
         "devices).\n";
  const bool ok = cycle_usb_power(
      plan,
      [&](const fs::path &hub, unsigned p, bool on) {
        return control(handles.fds.at(hub), 0x23, on ? 3 : 1, 8, p);
      },
      [] { std::this_thread::sleep_for(std::chrono::seconds(6)); });
  if (!ok)
    std::cerr << "[sysutils][wifi] USB hub power recovery failed; power "
                 "restoration was attempted.\n";
  return ok;
}

bool recover_zerocd_usb_power(const std::string &port) {
  if (!std::regex_match(port, std::regex("[0-9]+-[0-9]+(\\.[0-9]+)*")))
    return false;
  if (access(("/run/openhd/usb-power-recovery/" + port).c_str(), F_OK) == 0)
    return false;
  static std::set<std::string> requested;
  if (!requested.insert(port).second)
    return false;
  const char *helper = nullptr;
  for (const char *candidate : {"/usr/local/sbin/openhd-usb-power-recover",
                                "/usr/sbin/openhd-usb-power-recover"})
    if (access(candidate, X_OK) == 0) {
      helper = candidate;
      break;
    }
  if (!helper)
    return false;
  const char *runner = nullptr;
  const bool systemd = access("/run/systemd/system", F_OK) == 0;
  if (systemd) {
    for (const char *candidate : {"/usr/bin/systemd-run", "/bin/systemd-run"})
      if (access(candidate, X_OK) == 0) {
        runner = candidate;
        break;
      }
    // Do not leave a long power-off interval in SysUtils' own cgroup, which
    // may be killed five seconds after an administrator requests a restart.
    if (!runner)
      return false;
  }
  const auto unit = "--unit=openhd-usb-recovery-" + port;
  const pid_t child = fork();
  if (child == 0) {
    if (runner)
      execl(runner, runner, "--quiet", "--wait", "--collect", "-p",
            "TimeoutStopSec=120", unit.c_str(), helper, port.c_str(),
            static_cast<char *>(nullptr));
    else {
      setsid();
      execl(helper, helper, port.c_str(), static_cast<char *>(nullptr));
    }
    _exit(127);
  }
  if (child < 0)
    return false;
  int status = 0;
  for (int tick = 0; tick < 900; ++tick) {
    const auto result = waitpid(child, &status, WNOHANG);
    if (result == child)
      return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (result < 0 && errno != EINTR)
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  // The worker owns restoration. Never kill it just because discovery timed
  // out.
  std::cerr << "[sysutils][wifi] USB recovery worker still running for " << port
            << ".\n";
  std::thread([child] {
    int result;
    while (waitpid(child, &result, 0) < 0 && errno == EINTR) {
    }
  }).detach();
  return false;
}
} // namespace sysutil
