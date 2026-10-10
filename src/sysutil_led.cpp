/******************************************************************************
 * OpenHD
 *
 * Licensed under the GNU General Public License (GPL) Version 3.
 *
 * This software is provided "as-is," without warranty of any kind, express or
 * implied, including but not limited to the warranties of merchantability,
 * fitness for a particular purpose, and non-infringement. For details, see the
 * full license in the LICENSE file provided with this source code.
 *
 * Non-Military Use Only:
 * This software and its associated components are explicitly intended for
 * civilian and non-military purposes. Use in any military or defense
 * applications is strictly prohibited unless explicitly and individually
 * licensed otherwise by the OpenHD Team.
 *
 * Contributors:
 * A full list of contributors can be found at the OpenHD GitHub repository:
 * https://github.com/OpenHD
 *
 * ЖИ OpenHD, All Rights Reserved.
 ******************************************************************************/

#include "sysutil_led.h"
#include "sysutil_led_model.h"
#include "sysutil_platform.h"
#include "platforms_generated.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace sysutil {
namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}
std::string read(const std::filesystem::path& path) {
  std::ifstream f(path); std::string s; std::getline(f, s); return s;
}
bool write(const std::filesystem::path& path, const std::string& value) {
  std::ofstream f(path); f << value; return static_cast<bool>(f);
}
struct Device {
  std::filesystem::path path;
  std::string role, trigger, brightness, intensity, last_intensity;
  std::vector<std::string> channels;
  std::vector<int> channel_maxima;
  int maximum = 1, last = -1;
};
std::vector<Device> devices;
led::Model model;
std::mutex mutex;
std::thread worker;
std::atomic<bool> running{false};
int lock_fd = -1;
volatile std::sig_atomic_t preview_stop = 0;

bool acquire() {
  std::filesystem::create_directories("/run/openhd");
  lock_fd = ::open("/run/openhd/led.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0644);
  if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
    std::cerr << "[sysutils][led] LED controller already active or lock unavailable.\n";
    if (lock_fd >= 0) close(lock_fd);
    lock_fd = -1; return false;
  }
  return true;
}

void discover(const std::filesystem::path& root = "/sys/class/leds",
              const std::filesystem::path& profile_path = "/etc/openhd/leds.conf") {
  devices.clear();
  if (!std::filesystem::exists(root)) return;
  std::vector<std::pair<std::string, std::string>> selected;
  // Explicit, ordered assignment also supports independently exposed digital pixels.
  std::ifstream profile(profile_path);
  const bool explicit_profile = profile.is_open();
  std::string line;
  while (std::getline(profile, line)) {
    std::istringstream input(line); std::string role, name;
    if (!(input >> role >> name) || role[0] == '#') continue;
    if ((role == "primary" || role == "secondary" || role == "blue" || role == "pixel") &&
        name.find('/') == std::string::npos && name != "." && name != "..")
      selected.emplace_back(role, name);
  }
  if (!explicit_profile) {
    const int platform = platform_info().platform_type;
    auto choose = [&](const char* role, std::initializer_list<const char*> names) {
      for (const auto* name : names) if (std::filesystem::exists(root / name / "brightness")) {
        selected.emplace_back(role, name); return;
      }
    };
    if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_A ||
        platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_B ||
        platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_CM5)
      choose("primary", {"user-led2"});
    else if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_ZERO3W)
      choose("primary", {"board-led"});
    else if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_CM3)
      choose("primary", {"pi-led-green", "user-led"});
    else {
      // Never commandeer PWR, wlan/phy, disk or keyboard LEDs.
      choose("primary", {"ACT", "act", "led0", "work", "work-led", "user-led", "status"});
    }
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(root)) names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    bool rgb_group = false;
    for (const auto& name : names) {
      const auto green = name.find(":green:");
      if (name.find("openhd") == std::string::npos || green == std::string::npos) continue;
      auto red = name, blue = name;
      red.replace(green, 7, ":red:"); blue.replace(green, 7, ":blue:");
      if (std::filesystem::exists(root / red / "brightness") && std::filesystem::exists(root / blue / "brightness")) {
        selected = {{"primary", name}, {"secondary", red}, {"blue", blue}};
        rgb_group = true; break;
      }
    }
    for (const auto& name : names) {
      if (rgb_group) break;
      const bool owned = name.find("openhd") != std::string::npos;
      const bool digital = name.find("ws281") != std::string::npos || name.find("sk681") != std::string::npos;
      if (!owned && !digital) continue;
      if (std::filesystem::exists(root / name / "multi_intensity")) selected.emplace_back("pixel", name);
      else if (name.find("green") != std::string::npos &&
               std::none_of(selected.begin(), selected.end(), [](const auto& entry) { return entry.first == "primary"; }))
        selected.emplace_back("primary", name);
      else if (name.find("red") != std::string::npos) selected.emplace_back("secondary", name);
      else if (name.find("blue") != std::string::npos) selected.emplace_back("blue", name);
    }
  }
  for (const auto& [role, name] : selected) {
    if (std::any_of(devices.begin(), devices.end(), [&](const Device& d) { return d.path.filename() == name; })) continue;
    Device d; d.role = role; d.path = root / name;
    if (!std::filesystem::exists(d.path / "brightness")) continue;
    d.brightness = read(d.path / "brightness");
    std::istringstream maximum(read(d.path / "max_brightness")); maximum >> d.maximum;
    d.maximum = std::max(1, d.maximum);
    const auto triggers = read(d.path / "trigger");
    const auto a = triggers.find('['), b = triggers.find(']');
    if (a != std::string::npos && b != std::string::npos) d.trigger = triggers.substr(a + 1, b - a - 1);
    if (std::filesystem::exists(d.path / "multi_intensity")) {
      d.intensity = read(d.path / "multi_intensity");
      std::istringstream channels(read(d.path / "multi_index"));
      std::string channel; while (channels >> channel) d.channels.push_back(channel);
      if (d.channels.empty() || std::any_of(d.channels.begin(), d.channels.end(), [](const std::string& c) {
            return c != "red" && c != "green" && c != "blue";
          })) { std::cerr << "[sysutils][led] Unsupported multicolor channels: " << name << '\n'; continue; }
      std::istringstream maxima(read(d.path / "multi_max_intensity"));
      for (std::size_t i = 0; i < d.channels.size(); ++i) {
        int maximum = d.maximum;
        maxima >> maximum;
        d.channel_maxima.push_back(std::clamp(maximum, 0, d.maximum));
      }
      d.role = "pixel";
    }
    if (!d.trigger.empty() && !write(d.path / "trigger", "none")) continue;
    std::cerr << "[sysutils][led] " << name << " role=" << d.role << " max=" << d.maximum << '\n';
    devices.push_back(std::move(d));
  }
}

void restore() {
  for (const auto& d : devices) {
    if (!d.intensity.empty()) write(d.path / "multi_intensity", d.intensity);
    write(d.path / "brightness", d.brightness);
    if (!d.trigger.empty()) write(d.path / "trigger", d.trigger);
  }
  devices.clear();
  if (lock_fd >= 0) { close(lock_fd); lock_fd = -1; }
}

void output(led::View view, std::uint64_t elapsed) {
  const int pixels = std::count_if(devices.begin(), devices.end(), [](const Device& d) { return d.role == "pixel"; });
  const bool two = std::any_of(devices.begin(), devices.end(), [](const Device& d) { return d.role == "secondary"; }) &&
                   std::any_of(devices.begin(), devices.end(), [](const Device& d) { return d.role == "primary"; });
  const bool rgb = std::any_of(devices.begin(), devices.end(), [](const Device& d) { return d.role == "blue"; });
  const bool secondary_only = !two && !rgb && pixels == 0;
  int pixel = 0;
  for (auto& d : devices) {
    const auto f = led::render(view, elapsed, pixel, pixels);
    const double peak = std::max({f.color.r, f.color.g, f.color.b});
    const auto binary_color = [&](double value) { return value > 0 && value >= peak * 0.25 ? 1 : 0; };
    double level = d.role == "secondary" && !secondary_only ? f.secondary : f.primary;
    if (two && !rgb && d.role == "primary") {
      if (view.state == led::State::Warning) level = 1;
      if (view.state == led::State::Fault) level = 0;
    }
    if (rgb) level = d.role == "secondary" ? f.color.r : (d.role == "blue" ? f.color.b : f.color.g);
    if (d.role == "pixel") {
      ++pixel;
      std::string intensity;
      for (std::size_t i = 0; i < d.channels.size(); ++i) {
        const auto& channel = d.channels[i];
        const double value = channel == "red" ? f.color.r : channel == "green" ? f.color.g : f.color.b;
        if (!intensity.empty()) intensity += ' ';
        intensity += std::to_string(std::min(d.channel_maxima[i], d.maximum == 1 ? binary_color(value) :
            static_cast<int>(std::lround(value * d.maximum))));
      }
      if (intensity != d.last_intensity) {
        if (!write(d.path / "multi_intensity", intensity)) { std::cerr << "[sysutils][led] Color write failed: " << d.path << '\n'; running = false; return; }
        d.last_intensity = intensity;
      }
      level = 1;
    }
    const int value = d.maximum == 1 ? (rgb && d.role != "pixel" ? binary_color(level) : (level > 0 ? 1 : 0)) :
        static_cast<int>(std::lround(std::clamp(level, 0.0, 1.0) * d.maximum));
    if (value != d.last) {
      if (!write(d.path / "brightness", std::to_string(value))) { std::cerr << "[sysutils][led] Brightness write failed: " << d.path << '\n'; running = false; return; }
      d.last = value;
    }
  }
}

void loop() {
  auto last = led::State::Starting;
  auto begin = now_ms();
  while (running) {
    const auto now = now_ms(); led::View view;
    { std::lock_guard<std::mutex> guard(mutex); view = model.view(now); }
    if (view.state != last) { begin = now; last = view.state; }
    output(view, now - begin);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}
}  // namespace

void shutdown_leds() {
  running = false;
  if (worker.joinable()) worker.join();
  restore();
}
void init_leds() {
  if (worker.joinable() || !acquire()) return;
  discover();
  if (devices.empty()) { restore(); return; }
  running = true;
  worker = std::thread(loop);
  std::atexit(shutdown_leds);
}
void update_leds_from_status(const StatusSnapshot& s) {
  // Dedicated sources prevent unrelated local operations from replacing faults.
  std::string source = s.type == "sysutil.local" ? "local" : "openhd";
  if (source == "local") {
    source = s.state.substr(0, s.state.find('.'));
    if (source == "camera_setup") source = "camera";
  }
  std::lock_guard<std::mutex> guard(mutex);
  model.status(source, s.state, s.severity, s.has_error, now_ms(), s.ttl_ms);
}
void update_leds_runtime(const std::string& mode, bool operating, bool activity, bool recording, int ttl_ms) {
  std::lock_guard<std::mutex> guard(mutex);
  model.runtime(mode == "ground" ? led::Mode::Ground : mode == "record" ? led::Mode::Record : led::Mode::Air,
                operating, activity, recording, now_ms(), ttl_ms);
}
int preview_leds() {
  if (::geteuid() != 0) { std::cerr << "LED preview requires root.\n"; return 1; }
  if (!acquire()) return 1;
  discover();
  if (devices.empty()) { std::cerr << "No assigned LEDs found.\n"; restore(); return 1; }
  running = true;
  preview_stop = 0;
  const auto old_int = std::signal(SIGINT, [](int) { preview_stop = 1; });
  const auto old_term = std::signal(SIGTERM, [](int) { preview_stop = 1; });
  for (const auto state : {led::State::Starting, led::State::Waiting, led::State::Operating,
                           led::State::Warning, led::State::Fault, led::State::Updating, led::State::Shutdown}) {
    for (const auto mode : {led::Mode::Air, led::Mode::Ground, led::Mode::Record}) {
      if (state != led::State::Operating && mode != led::Mode::Air) continue;
      std::cout << "LED preview: " << led::name(state) << " mode=" << static_cast<int>(mode) << std::endl;
      for (int t = 0; t < 4000 && running && !preview_stop; t += 50) {
        output({state, mode, true}, t);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
  }
  const bool ok = running;
  running = false; restore();
  std::signal(SIGINT, old_int); std::signal(SIGTERM, old_term);
  return preview_stop ? 130 : ok ? 0 : 1;
}
}  // namespace sysutil
