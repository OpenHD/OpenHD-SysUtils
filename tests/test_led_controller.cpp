#include "../src/sysutil_led.cpp"
#include <stdexcept>

namespace sysutil {
const PlatformInfo& platform_info() { static PlatformInfo p; return p; }
}
using namespace sysutil;
using namespace sysutil::led;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
  Model m;
  Model restarted;
  restarted.runtime(Mode::Ground, true, true, false, 0);
  check(restarted.view(1).state == State::Operating, "runtime must recover a restarted controller");
  m.status("openhd", "ready", 0, false, 0);
  m.runtime(Mode::Air, true, true, false, 0);
  check(m.view(100).state == State::Operating, "Air TX must stay operating without peer");
  m.status("camera", "camera_setup", 0, false, 100, 1000);
  check(m.view(200).state == State::Starting, "setup overlay missing");
  check(m.view(1200).state == State::Operating, "setup must return to base");
  m.status("openhd", "error", 2, true, 1300);
  m.status("camera", "camera_setup", 0, false, 1400);
  m.runtime(Mode::Ground, true, true, false, 1400);
  check(m.view(1500).state == State::Fault, "notifications/activity must not erase fault");
  m.status("openhd", "ready", 0, false, 1600);
  check(m.view(5000).state == State::Operating, "ready must recover own fault");
  check(m.view(9000).state == State::Waiting, "stale runtime must expire");
  m.status("updating", "updating", 0, false, 10000);
  check(m.view(100000).state == State::Updating, "long update must remain visible");
  m.status("updating", "updating.complete", 0, false, 100001);
  check(m.view(100002).state == State::Waiting, "update completion must return to base");
  m.runtime(Mode::Record, false, false, true, 100010);
  check(m.view(100011).state == State::Operating, "record-only mode must indicate active recording");
  for (int t = 0; t < 4000; t += 25) {
    const auto f = render({State::Operating, Mode::Air, true}, t);
    check(f.primary == 1 && f.color.g > 0, "healthy operation must never go dark");
  }
  check(render({State::Shutdown, Mode::Air, false}, 2500).primary == 0, "shutdown must finish off");
  const auto a = render({State::Operating, Mode::Air, true}, 400, 1, 4);
  const auto g = render({State::Operating, Mode::Ground, true}, 400, 1, 4);
  check(a.color.g != g.color.g, "pixel animation must express direction");

  const auto root = std::filesystem::temp_directory_path() / ("openhd-led-test-" + std::to_string(getpid()));
  std::filesystem::create_directories(root);
  auto device = [&](const char* name, int maximum, const char* channels = "") {
    const auto path = root / name; std::filesystem::create_directory(path);
    write(path / "brightness", "0"); write(path / "max_brightness", std::to_string(maximum));
    write(path / "trigger", "none [heartbeat]");
    if (*channels) { write(path / "multi_index", channels); write(path / "multi_intensity", "0 0 0"); }
  };
  device("ACT", 1); device("PWR", 1); device("phy0:green:wlan", 1);
  discover(root, root / "absent.conf");
  check(devices.size() == 1, "fallback must own only known status LEDs");
  output({State::Operating, Mode::Air, true}, 0);
  check(read(root / "ACT/brightness") == "1", "single LED ready must light");
  check(read(root / "PWR/trigger") == "none [heartbeat]", "power trigger must remain untouched");
  check(read(root / "phy0:green:wlan/trigger") == "none [heartbeat]", "network trigger must remain untouched");
  restore();
  check(read(root / "ACT/trigger") == "heartbeat", "owned trigger must restore");
  device("rgb0", 255, "blue red green");
  write(root / "profile", "pixel rgb0\n");
  discover(root, root / "profile");
  output({State::Operating, Mode::Air, false}, 0);
  check(read(root / "rgb0/multi_intensity") == "11 0 89", "RGB must honor driver channel order and range");
  restore();
  write(root / "profile", "primary ACT\nsecondary PWR\n");
  discover(root, root / "profile");
  output({State::Fault, Mode::Air, false}, 0);
  check(read(root / "ACT/brightness") == "0" && read(root / "PWR/brightness") == "1", "two-LED fault roles wrong");
  output({State::Warning, Mode::Air, true}, 0);
  check(read(root / "ACT/brightness") == "1", "warning must keep healthy status lit");
  restore();
  write(root / "profile", "secondary PWR\n");
  discover(root, root / "profile");
  output({State::Operating, Mode::Air, false}, 0);
  check(read(root / "PWR/brightness") == "1", "a lone error-assigned LED must fall back to status patterns");
  restore();
  device("openhd:red:usr", 1); device("openhd:green:usr", 1); device("openhd:blue:usr", 1);
  // Explicit assignment is authoritative, including an intentionally empty file.
  write(root / "profile", ""); discover(root, root / "profile");
  check(devices.empty(), "empty profile must disable ownership");
  discover(root, root / "absent.conf");
  output({State::Operating, Mode::Air, false}, 0);
  check(read(root / "openhd:green:usr/brightness") == "1", "binary RGB green base must stay lit");
  check(read(root / "openhd:blue:usr/brightness") == "0", "binary RGB must approximate green without blue");
  restore();
  std::filesystem::remove_all(root);
  handle_status_message(R"({"type":"indicator.set","state":"READY"})");
  handle_status_message(R"({"type":"indicator.runtime","mode":"ground","operating":true,"activity":true,"recording":false})");
  check(model.view(now_ms()).state == State::Operating, "structured runtime parsing failed");
  handle_status_message(R"({"type":"indicator.set","state":"ERROR","description":"No camera detected"})");
  handle_status_message(R"({"type":"indicator.status","description":"ordinary notification"})");
  check(model.view(now_ms()).state == State::Fault, "status notifications must preserve LED fault");
  handle_status_message(R"({"type":"indicator.clear"})");
  check(model.view(now_ms()).state == State::Operating, "clear must recover own LED fault");
}
