#include "sysutil_startup_hold.h"
#include <fstream>
#include <stdexcept>

int main() {
 namespace fs = std::filesystem;
 auto root = fs::temp_directory_path() / ("openhd-hold-test-" + std::to_string(getpid()));
 fs::create_directory(root);
 auto marker = root / "hold.pid";
 {
   sysutil::ScopedStartupHold first(marker);
   if (!first.acquired() || !fs::exists(marker)) return 1;
   { sysutil::ScopedStartupHold other(marker); if (other.acquired()) return 2; }
   if (!fs::exists(marker)) return 3;
 }
 if (fs::exists(marker)) return 4;
 try {
   sysutil::ScopedStartupHold hold(marker);
   if (!hold.acquired()) return 5;
   throw std::runtime_error("simulated failed partition operation");
 } catch (const std::runtime_error&) {}
 if (fs::exists(marker)) return 6;
 std::ofstream(marker) << "existing operation";
 { sysutil::ScopedStartupHold existing(marker); if (existing.acquired()) return 7; }
 if (!fs::exists(marker)) return 8;
 fs::remove_all(root);
 return 0;
}
