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
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platforms_generated.h"
#include "sysutil_platform.h"

#ifdef OPENHD_HAVE_X21_LED
extern "C" {
#include <ohdled.h>
}
#endif

namespace sysutil {
namespace {

enum class LedPatternType {
  Off,
  Solid,
  Blink,
  Alternate,
  Rainbow
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
  int repeat_count = -1;
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
int g_pattern_id = 0;

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool contains_any_token(const std::string& haystack_lower,
                        const std::vector<std::string>& needles_lower) {
  for (const auto& needle : needles_lower) {
    if (!needle.empty() && haystack_lower.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

int find_led_by_name(const LedLayout& layout,
                     const std::vector<std::string>& preferred_names) {
  for (const auto& preferred : preferred_names) {
    for (int i = 0; i < static_cast<int>(layout.leds.size()); ++i) {
      if (layout.leds[i].name == preferred) {
        return i;
      }
    }
  }
  return -1;
}

int find_led_by_token(const LedLayout& layout,
                      const std::vector<std::string>& preferred_tokens) {
  for (const auto& token : preferred_tokens) {
    for (int i = 0; i < static_cast<int>(layout.leds.size()); ++i) {
      const auto lower = to_lower(layout.leds[i].name);
      if (lower.find(token) != std::string::npos) {
        return i;
      }
    }
  }
  return -1;
}

std::string build_status_text_lower(const StatusSnapshot& status) {
  std::string combined;
  combined.reserve(status.state.size() + status.description.size() +
                   status.message.size() + status.type.size() + 8);
  combined += status.state;
  combined.push_back(' ');
  combined += status.description;
  combined.push_back(' ');
  combined += status.message;
  combined.push_back(' ');
  combined += status.type;
  return to_lower(combined);
}

enum class ErrorKind {
  WifiCardMissing,
  CameraMissing,
  Other
};

ErrorKind classify_error_kind(const StatusSnapshot& status) {
  const auto text = build_status_text_lower(status);
  const std::vector<std::string> wifi_card_missing_tokens = {
      "no openhd wifibroadcast card found",
      "no openhd-compatible card found",
      "no wifi cards detected",
      "no wi-fi cards detected",
      "openhd-compatible card not found"};
  if (contains_any_token(text, wifi_card_missing_tokens)) {
    return ErrorKind::WifiCardMissing;
  }

  const std::vector<std::string> camera_missing_tokens = {
      "no physical camera detected",
      "no camera detected",
      "camera not found",
      "camera setup failed",
      "dummy camera configuration",
      "unable to apply camera configuration"};
  if (contains_any_token(text, camera_missing_tokens)) {
    return ErrorKind::CameraMissing;
  }
  return ErrorKind::Other;
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

  const int platform = platform_info().platform_type;
  int green_idx = find_led_by_token(layout, {"green"});
  int red_idx = find_led_by_token(layout, {"red"});

  if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_A ||
      platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_ROCK5_B ||
      platform == X_PLATFORM_TYPE_ROCKCHIP_RK3588_RADXA_CM5) {
    const int rock_user_led = find_led_by_name(layout, {"user-led2"});
    if (rock_user_led >= 0) {
      green_idx = rock_user_led;
    }
  } else if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_ZERO3W) {
    const int board_led = find_led_by_name(layout, {"board-led"});
    if (board_led >= 0) {
      green_idx = board_led;
    }
  } else if (platform == X_PLATFORM_TYPE_ROCKCHIP_RK3566_RADXA_CM3) {
    const int cm3_green = find_led_by_name(layout, {"pi-led-green", "user-led"});
    const int cm3_red = find_led_by_name(layout, {"pwr-led-red"});
    if (cm3_green >= 0) {
      green_idx = cm3_green;
    }
    if (cm3_red >= 0) {
      red_idx = cm3_red;
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
  // Distinct error frequencies:
  // - Wi-Fi card missing: fast
  // - Camera missing: medium
  // - Other errors: slow
  LedPattern wifi_missing_error_pattern{LedPatternType::Blink, LedTarget::Both,
                                        120, 120, -1};
  LedPattern camera_missing_error_pattern{LedPatternType::Blink, LedTarget::Both,
                                          300, 300, -1};
  LedPattern other_error_pattern{LedPatternType::Blink, LedTarget::Both, 700, 700,
                                 -1};
  LedPattern warn_pattern{LedPatternType::Blink, LedTarget::Secondary, 200, 200, -1};
  LedPattern starting_pattern{LedPatternType::Blink, LedTarget::Primary, 200, 200, -1};
  LedPattern ready_pattern{LedPatternType::Rainbow, LedTarget::Primary, 200, 200, -1};
  LedPattern stopped_pattern{LedPatternType::Off, LedTarget::Both, 200, 200, -1};
  LedPattern partition_pattern{LedPatternType::Blink, LedTarget::Both, 120, 120, -1};
  LedPattern sysutils_started{LedPatternType::Blink, LedTarget::Both, 120, 120, 3};
  LedPattern camera_setup{LedPatternType::Blink, LedTarget::Both, 120, 120, 4};
  LedPattern reboot_initiated{LedPatternType::Blink, LedTarget::Both, 2000, 200, 1};
  LedPattern updating_pattern{LedPatternType::Alternate, LedTarget::Both, 120, 120, -1};

  if (!status.has_data) {
    return stopped_pattern;
  }
  if (status.has_error || status.severity >= 2) {
    switch (classify_error_kind(status)) {
      case ErrorKind::WifiCardMissing:
        return wifi_missing_error_pattern;
      case ErrorKind::CameraMissing:
        return camera_missing_error_pattern;
      case ErrorKind::Other:
      default:
        return other_error_pattern;
    }
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
      {"update", updating_pattern},
      {"sysutils.started", sysutils_started},
      {"camera_setup", camera_setup},
      {"reboot", reboot_initiated},
      {"starting", starting_pattern},
      {"boot", starting_pattern},
      {"ready", ready_pattern},
      {"link_lost", warn_pattern},
      {"error", other_error_pattern},
      {"stopped", stopped_pattern},
  };

  for (const auto& rule : rules) {
    if (state.find(rule.key) != std::string::npos) {
      return rule.pattern;
    }
  }

  return ready_pattern;
}

#ifdef OPENHD_HAVE_X21_LED
constexpr char kX21LedDevice[] = "/dev/ttyS3";
int g_x21_fd = -1;

led_color x21_color_for_target(LedTarget target) {
  switch (target) {
    case LedTarget::Secondary:
      return led_color{255, 0, 0};
    case LedTarget::Both:
      return led_color{255, 255, 0};
    case LedTarget::Primary:
    default:
      return led_color{0, 255, 0};
  }
}

std::uint16_t x21_delay_ms(int ms) {
  if (ms < 0) {
    return 0;
  }
  if (ms > 0xFFFF) {
    return 0xFFFF;
  }
  return static_cast<std::uint16_t>(ms);
}

bool init_x21_leds() {
  g_x21_fd = led_open(kX21LedDevice);
  if (g_x21_fd < 0) {
    return false;
  }
  led_off(g_x21_fd);
  return true;
}

void apply_x21_rainbow() {
  static const led_color kRainbow[] = {
      {255, 0, 0},   {255, 128, 0}, {255, 255, 0}, {128, 255, 0},
      {0, 255, 0},   {0, 255, 128}, {0, 255, 255}, {0, 128, 255},
      {0, 0, 255},   {128, 0, 255}, {255, 0, 255}, {255, 0, 128},
  };
  constexpr std::size_t kCount = sizeof(kRainbow) / sizeof(kRainbow[0]);
  led_anim_frame frames[kCount];
  for (std::size_t i = 0; i < kCount; ++i) {
    frames[i].color = kRainbow[i];
    frames[i].delay_ms = 150;
  }
  led_breathe(g_x21_fd, frames, kCount, 1, 0, 0);
}

void apply_x21_pattern(const LedPattern& pattern) {
  if (g_x21_fd < 0) {
    return;
  }
  switch (pattern.type) {
    case LedPatternType::Off:
      led_off(g_x21_fd);
      break;
    case LedPatternType::Solid:
      led_static_color(g_x21_fd, x21_color_for_target(pattern.target), 1, 0, 0);
      break;
    case LedPatternType::Blink: {
      led_anim_frame frames[2];
      frames[0].color = x21_color_for_target(pattern.target);
      frames[0].delay_ms = x21_delay_ms(pattern.on_ms);
      frames[1].color = led_color{0, 0, 0};
      frames[1].delay_ms = x21_delay_ms(pattern.off_ms);
      led_blink(g_x21_fd, frames, 2, 1, 0, 0);
      break;
    }
    case LedPatternType::Alternate: {
      led_anim_frame frames[2];
      frames[0].color = x21_color_for_target(LedTarget::Primary);
      frames[0].delay_ms = x21_delay_ms(pattern.on_ms);
      frames[1].color = x21_color_for_target(LedTarget::Secondary);
      frames[1].delay_ms = x21_delay_ms(pattern.off_ms);
      led_blink(g_x21_fd, frames, 2, 1, 0, 0);
      break;
    }
    case LedPatternType::Rainbow:
      apply_x21_rainbow();
      break;
  }
}
#endif  // OPENHD_HAVE_X21_LED

void worker_loop() {
  int last_pattern_id = -1;
  int remaining = -1;
  while (g_running) {
    LedPattern pattern;
    int pattern_id = 0;
    {
      std::lock_guard<std::mutex> lock(g_pattern_mutex);
      pattern = g_current_pattern;
      pattern_id = g_pattern_id;
    }
    if (pattern_id != last_pattern_id) {
      last_pattern_id = pattern_id;
      remaining = pattern.repeat_count;
    }
    if (pattern.repeat_count > 0 && remaining == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      continue;
    }
    switch (pattern.type) {
      case LedPatternType::Off:
        set_all_off();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        break;
      case LedPatternType::Solid:
      case LedPatternType::Rainbow:
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
    if (pattern.repeat_count > 0 && remaining > 0) {
      --remaining;
    }
  }
}

}  // namespace

void init_leds() {
#ifdef OPENHD_HAVE_X21_LED
  if (platform_info().platform_type == X_PLATFORM_TYPE_OPENHD_X21) {
    if (init_x21_leds()) {
      return;
    }
  }
#endif
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
#ifdef OPENHD_HAVE_X21_LED
  if (g_x21_fd >= 0) {
    apply_x21_pattern(select_pattern_from_status(status));
    return;
  }
#endif
  if (g_layout.leds.empty()) {
    return;
  }
  const auto next_pattern = select_pattern_from_status(status);
  std::lock_guard<std::mutex> lock(g_pattern_mutex);
  g_current_pattern = next_pattern;
  ++g_pattern_id;
}

}  // namespace sysutil
