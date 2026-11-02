#include "sysutil_usb_power.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unistd.h>
namespace fs = std::filesystem;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void write(const fs::path &p, const std::string &s) {
  if (!fs::is_directory(p.parent_path()))
    fs::create_directories(p.parent_path());
  std::ofstream(p) << s;
}
int main() {
  const auto base = fs::temp_directory_path() /
                    ("openhd-usb-power-" + std::to_string(getpid()));
  const auto root = base / "usb";
  try {
    fs::create_directories(root);
    for (const auto &bus : {"usb1", "usb2"}) {
      fs::create_directories(base / "controller" / bus);
      fs::create_directory_symlink(base / "controller" / bus, root / bus);
    }
    write(root / "usb2" / "version", "3.00");
    write(root / "1-1" / "idVendor", "2109");
    write(root / "1-1" / "idProduct", "3431");
    write(root / "1-1.3" / "idVendor", "0bda");
    write(root / "1-1.3" / "idProduct", "1a2b");
    write(root / "1-1.3:1.0" / "bInterfaceClass", "08");
    bool individual = true, supported = true;
    const sysutil::UsbHubQuery query = [&](const fs::path &) {
      return std::optional<sysutil::UsbHubPowerInfo>{
          {4, individual, supported}};
    };
    auto plan = sysutil::plan_usb_wifi_power_cycle(root, "1-1.3", false, query);
    require(plan.size() == 1 && plan[0].ports == std::vector<unsigned>{3},
            "individual hub must isolate radio port");
    write(root / "1-1.2:1.0" / "bInterfaceClass", "08");
    require(!sysutil::plan_usb_wifi_power_cycle(root, "1-1.3", false, query)
                 .empty(),
            "unaffected storage must not block isolated recovery");
    individual = false;
    require(
        sysutil::plan_usb_wifi_power_cycle(root, "1-1.3", false, query).empty(),
        "ganged hub must protect adjacent USB storage");
    fs::remove_all(root / "1-1.2:1.0");
    plan = sysutil::plan_usb_wifi_power_cycle(root, "1-1.3", false, query);
    require(plan.size() == 1 && plan[0].ports.size() == 4,
            "ganged hub requires all ports off");
    individual = true;
    plan = sysutil::plan_usb_wifi_power_cycle(root, "1-1.3", true, query);
    require(plan.size() == 2 && plan[0].ports.size() == 4 &&
                plan[1].ports.size() == 4,
            "Pi4 must cycle shared USB2 and USB3 rails");
    write(root / "2-2.2:1.0" / "bInterfaceClass", "08");
    require(
        sysutil::plan_usb_wifi_power_cycle(root, "1-1.3", true, query).empty(),
        "Pi4 shared rail must protect USB3 storage too");
    fs::remove_all(root / "2-2.2:1.0");
    write(root / "1-1.3.2:1.0" / "bInterfaceClass", "08");
    require(
        sysutil::plan_usb_wifi_power_cycle(root, "1-1.3", false, query).empty(),
        "storage below target port must be protected");
    fs::remove_all(root / "1-1.3.2:1.0");
    supported = false;
    require(
        sysutil::plan_usb_wifi_power_cycle(root, "1-1.3", false, query).empty(),
        "unsupported switches must not be used");
    supported = true;
    require(sysutil::plan_usb_wifi_power_cycle(root, "../usb1", false, query)
                .empty(),
            "invalid topology must be rejected");
    require(
        sysutil::plan_usb_wifi_power_cycle(root, "1-1.5", false, query).empty(),
        "out of range port must be rejected");
    plan = sysutil::plan_usb_wifi_power_cycle(root, "1-1.3", true, query);
    std::vector<std::tuple<std::string, unsigned, bool>> calls;
    unsigned offs = 0, ons = 0;
    bool waited = false;
    const auto power = [&](const fs::path &p, unsigned port, bool on) {
      calls.emplace_back(p.filename().string(), port, on);
      if (on) {
        ++ons;
        return true;
      }
      return ++offs != 2;
    };
    require(!sysutil::cycle_usb_power(plan, power, [&] { waited = true; }),
            "failed power-off must fail recovery");
    require(!waited && offs == 2 && ons == 2,
            "ambiguous power-off must immediately restore every touched port");
    offs = ons = 0;
    calls.clear();
    require(sysutil::cycle_usb_power(
                plan,
                [&](const fs::path &, unsigned, bool on) {
                  if (on) {
                    ++ons;
                    require(waited, "power restored before off interval");
                    return true;
                  }
                  ++offs;
                  return true;
                },
                [&] {
                  require(offs == 8,
                          "shared rail must be fully off before waiting");
                  waited = true;
                }),
            "full cycle failed");
    require(ons == 8, "all shared rail switches must be restored");
    unsigned on_attempts = 0;
    require(sysutil::cycle_usb_power(
                {plan[0]},
                [&](const fs::path &, unsigned, bool on) {
                  return !on || ++on_attempts % 2 == 0;
                },
                [] {}),
            "transient restore failures must be retried");
    bool restored = false;
    try {
      sysutil::cycle_usb_power(
          {{root / "1-1", {3}}},
          [&](const fs::path &, unsigned, bool on) {
            restored = on;
            return true;
          },
          [] { throw std::runtime_error("wait failed"); });
    } catch (const std::runtime_error &) {
    }
    require(restored, "exception after power-off must restore power");
    fs::remove_all(base);
    std::cout << "USB power recovery tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    fs::remove_all(base);
    std::cerr << e.what() << '\n';
    return 1;
  }
}
