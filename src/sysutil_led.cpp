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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sysutil {
namespace {

enum class LedPatternType {
  Off,
  Solid,
  Blink,
  Alternate
};

enum class LedTarget {
  Primary,
  Secondary,
  Both
};

struct LedPattern {
  LedPatternType type = LedPatternType::Off;
  LedTarget target = LedTarget::Primary;
  int on_ms = 100;
  int off_ms = 100;
};

struct LedDevice {
  std::string name;
  std::string brightness_path;
  bool active_low = false;
};

struct LedLayout {
  std::vector<LedDevice> leds;
  int primary_idx = -1;
  int secondary_idx = -1;
};

LedLayout g_layout;
std::atomic<bool> g_running{false};
std::thread g_worker;
std::mutex g_pattern_mutex;
LedPattern g_current_pattern{};

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool write_file(const std::string& path, const std::string& value) {
  std::ofstream file(path);
  if (!file) {
    return false;
  }
  file << value;
  return static_cast<bool>(file);
}

bool read_bool_file(const std::string& path, bool& out) {
  std::ifstream file(path);
  if (!file) {
    return false;
  }
  int value = 0;
  file >> value;
  out = (value != 0);
  return true;
}

void set_led_state(int idx, bool on) {
  if (idx < 0 || idx >= static_cast<int>(g_layout.leds.size())) {
    return;
  }
  const auto& led = g_layout.leds[idx];
  const bool effective_on = led.active_low ? !on : on;
  write_file(led.brightness_path, effective_on ? "1" : "0");
}

void set_targets(LedTarget target, bool on) {
  if (target == LedTarget::Primary || target == LedTarget::Both) {
    set_led_state(g_layout.primary_idx, on);
  }
  if (target == LedTarget::Secondary || target == LedTarget::Both) {
    set_led_state(g_layout.secondary_idx, on);
  }
}

void set_all_off() {
  for (int i = 0; i < static_cast<int>(g_layout.leds.size()); ++i) {
    set_led_state(i, false);
  }
}

void set_solid(const LedPattern& pattern) {
  set_all_off();
  set_targets(pattern.target, true);
}

void blink_once(const LedPattern& pattern) {
  set_targets(pattern.target, true);
  std::this_thread::sleep_for(std::chrono::milliseconds(pattern.on_ms));
  set_targets(pattern.target, false);
  std::this_thread::sleep_for(std::chrono::milliseconds(pattern.off_ms));
}

void alternate_once(const LedPattern& pattern) {
  if (g_layout.primary_idx < 0 || g_layout.secondary_idx < 0 ||
      g_layout.primary_idx == g_layout.secondary_idx) {
    blink_once(pattern);
    return;
  }
  set_led_state(g_layout.primary_idx, true);
  set_led_state(g_layout.secondary_idx, false);
  std::this_thread::sleep_for(std::chrono::milliseconds(pattern.on_ms));
  set_led_state(g_layout.primary_idx, false);
  set_led_state(g_layout.secondary_idx, true);
  std::this_thread::sleep_for(std::chrono::milliseconds(pattern.off_ms));
}

LedLayout discover_leds() {
  LedLayout layout;
  std::error_code ec;
  const std::filesystem::path root("/sys/class/leds");
  if (!std::filesystem::exists(root, ec)) {
    return layout;
  }

  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (ec || !entry.is_directory()) {
      continue;
    }
    const auto name = entry.path().filename().string();
    const auto brightness = entry.path() / "brightness";
    if (!std::filesystem::exists(brightness, ec)) {
      continue;
    }
    LedDevice device;
    device.name = name;
    device.brightness_path = brightness.string();
    const auto active_low_path = entry.path() / "active_low";
    bool active_low = false;
    if (read_bool_file(active_low_path.string(), active_low)) {
      device.active_low = active_low;
    }
    const auto trigger_path = entry.path() / "trigger";
    if (std::filesystem::exists(trigger_path, ec)) {
      (void)write_file(trigger_path.string(), "none");
    }
    layout.leds.push_back(std::move(device));
  }

  int green_idx = -1;
  int red_idx = -1;
  for (int i = 0; i < static_cast<int>(layout.leds.size()); ++i) {
    const auto lower = to_lower(layout.leds[i].name);
    if (green_idx < 0 && lower.find("green") != std::string::npos) {
      green_idx = i;
    }
    if (red_idx < 0 && lower.find("red") != std::string::npos) {
      red_idx = i;
    }
  }

  if (green_idx >= 0) {
    layout.primary_idx = green_idx;
  } else if (!layout.leds.empty()) {
    layout.primary_idx = 0;
  }

  if (red_idx >= 0) {
    layout.secondary_idx = red_idx;
  } else if (layout.leds.size() >= 2) {
    layout.secondary_idx = 1;
  } else {
    layout.secondary_idx = layout.primary_idx;
  }

  return layout;
}

LedPattern select_pattern_from_status(const StatusSnapshot& status) {
  LedPattern error_pattern{LedPatternType::Alternate, LedTarget::Both, 80, 80};
  LedPattern warn_pattern{LedPatternType::Blink, LedTarget::Secondary, 200, 200};
  LedPattern starting_pattern{LedPatternType::Blink, LedTarget::Primary, 200, 200};
  LedPattern ready_pattern{LedPatternType::Solid, LedTarget::Primary, 200, 200};
  LedPattern stopped_pattern{LedPatternType::Off, LedTarget::Both, 200, 200};
  LedPattern partition_pattern{LedPatternType::Blink, LedTarget::Both, 120, 120};

  if (!status.has_data) {
    return stopped_pattern;
  }
  if (status.has_error || status.severity >= 2) {
    return error_pattern;
  }
  if (status.severity == 1) {
    return warn_pattern;
  }

  const auto state = to_lower(status.state);
  struct Rule {
    const char* key;
    LedPattern pattern;
  };
  const std::vector<Rule> rules = {
      {"partition", partition_pattern},
      {"starting", starting_pattern},
      {"boot", starting_pattern},
      {"ready", ready_pattern},
      {"link_lost", warn_pattern},
      {"error", error_pattern},
      {"stopped", stopped_pattern},
  };

  for (const auto& rule : rules) {
    if (state.find(rule.key) != std::string::npos) {
      return rule.pattern;
    }
  }

  return ready_pattern;
}

void worker_loop() {
  while (g_running) {
    LedPattern pattern;
    {
      std::lock_guard<std::mutex> lock(g_pattern_mutex);
      pattern = g_current_pattern;
    }
    switch (pattern.type) {
      case LedPatternType::Off:
        set_all_off();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        break;
      case LedPatternType::Solid:
        set_solid(pattern);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        break;
      case LedPatternType::Blink:
        blink_once(pattern);
        break;
      case LedPatternType::Alternate:
        alternate_once(pattern);
        break;
    }
  }
}

}  // namespace

void init_leds() {
  g_layout = discover_leds();
  if (g_layout.leds.empty()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_pattern_mutex);
    g_current_pattern = LedPattern{};
  }
  g_running = true;
  g_worker = std::thread(worker_loop);
  g_worker.detach();
}

void update_leds_from_status(const StatusSnapshot& status) {
  if (g_layout.leds.empty()) {
    return;
  }
  const auto next_pattern = select_pattern_from_status(status);
  std::lock_guard<std::mutex> lock(g_pattern_mutex);
  g_current_pattern = next_pattern;
}

}  // namespace sysutil
