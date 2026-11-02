#include "sysutil_usb_power.h"
#include <csignal>
#include <iostream>
#include <string>
#include <unistd.h>
int main(int argc, char **argv) {
  const bool check_only = argc == 3 && std::string(argv[1]) == "--check";
  if ((argc != 2 && !check_only) || geteuid() != 0) {
    std::cerr << "Usage (root): openhd-usb-power-recover [--check] USB-PORT\n";
    return 2;
  }
  // Finish restoring the hub even if an operator terminates a waiting client.
  signal(SIGTERM, SIG_IGN);
  signal(SIGINT, SIG_IGN);
  signal(SIGHUP, SIG_IGN);
  try {
    return sysutil::perform_zerocd_usb_power_recovery(argv[check_only ? 2 : 1],
                                                      check_only)
               ? 0
               : 1;
  } catch (const std::exception &e) {
    std::cerr << "USB recovery failed: " << e.what() << '\n';
    return 1;
  }
}
