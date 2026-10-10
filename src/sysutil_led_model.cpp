#include "sysutil_led_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace sysutil::led {
namespace {
int priority(State s) {
  switch (s) {
    case State::Shutdown: return 6;
    case State::Updating: return 5;
    case State::Fault: return 4;
    case State::Warning: return 3;
    default: return 0;
  }
}
Color scale(Color c, double v) { return {c.r * v, c.g * v, c.b * v}; }
bool double_pulse(std::uint64_t t) { t %= 2000; return t < 140 || (t >= 280 && t < 420); }
}

void Model::status(const std::string& source, const std::string& raw, int severity,
                   bool error, std::uint64_t now, int ttl_ms) {
  std::string state = raw;
  std::transform(state.begin(), state.end(), state.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  const auto has = [&](const char* token) { return state.find(token) != std::string::npos; };
  if (state == "clear") { events_.erase(source); return; }
  if (state == "stopped" || state == "reboot" || state == "shutdown") {
    base_ = State::Shutdown;
    events_[source] = {State::Shutdown, now + 2500};
    return;
  }
  if (error || severity >= 2 || state == "error") {
    // Persistent until the same subsystem explicitly reports recovery/clear.
    events_[source] = {State::Fault, UINT64_MAX};
    return;
  }
  if (state == "ready" || state == "starting" || state == "booting" || state == "sysutils.started") {
    base_ = state == "ready" ? State::Operating : State::Starting;
    events_.erase(source);
    return;
  }
  if (has("complete") || has("applied")) { events_.erase(source); return; }
  // Ordinary notifications cannot erase a blocking fault.
  auto it = events_.find(source);
  if (it != events_.end() && it->second.state == State::Fault) return;
  const auto expiry = now + static_cast<std::uint64_t>(std::clamp(ttl_ms, 500, 60000));
  if (severity == 1 || state == "link_lost") events_[source] = {State::Warning, expiry};
  else if (has("updat") || has("partition") || has("format") || has("migrat"))
    events_[source] = {State::Updating, state == "updating" ? UINT64_MAX : expiry};
  else if (state == "camera_setup") events_[source] = {State::Starting, expiry};
}

void Model::runtime(Mode mode, bool operating, bool activity, bool recording,
                    std::uint64_t now, int ttl_ms) {
  // Runtime is emitted only after OpenHD initialization, and also recovers the
  // base state if SysUtils restarted after the one-shot READY notification.
  if (base_ == State::Starting) base_ = State::Operating;
  mode_ = mode;
  operating_ = operating;
  activity_ = activity;
  recording_ = recording;
  runtime_until_ = now + static_cast<std::uint64_t>(std::clamp(ttl_ms, 500, 15000));
}

View Model::view(std::uint64_t now) const {
  State state = base_;
  if (state == State::Operating && runtime_until_ != 0)
    state = now < runtime_until_ && (operating_ || (mode_ == Mode::Record && recording_))
        ? State::Operating : State::Waiting;
  for (const auto& [source, event] : events_)
    if (now < event.until && (priority(event.state) > priority(state) ||
        (event.state == State::Starting && priority(state) == 0))) state = event.state;
  return {state, mode_, now < runtime_until_ && (activity_ || (mode_ == Mode::Record && recording_))};
}

Frame render(View v, std::uint64_t t, int pixel, int pixels) {
  Frame f;
  const double pi = 3.14159265358979323846;
  const double breath = 0.20 + 0.65 * (1.0 - std::cos(2 * pi * (t % 2400) / 2400.0)) / 2;
  switch (v.state) {
    case State::Starting:
      f.primary = t % 1600 < 800; f.color = scale({0.08, 0.2, 1}, breath); break;
    case State::Waiting:
      f.primary = double_pulse(t); f.color = scale({1, 0.38, 0}, breath); break;
    case State::Operating: {
      f.primary = 1;
      double glow = 0.35;
      if (v.activity) {
        const double direction = v.mode == Mode::Ground ? -1.0 : 1.0;
        const double phase = 2 * pi * (t % 1600) / 1600.0 -
            direction * 2 * pi * pixel / std::max(1, pixels);
        glow += 0.45 * std::pow((1 + std::cos(phase)) / 2, 4);
      }
      f.color = scale(v.mode == Mode::Record ? Color{0, 0.85, 1} : Color{0, 1, 0.12}, glow);
      break;
    }
    case State::Warning:
      f.primary = double_pulse(t) ? 0 : 1;
      f.secondary = double_pulse(t);
      f.color = double_pulse(t) ? Color{0.9, 0.35, 0} : Color{0, 0.35, 0.04}; break;
    case State::Fault: {
      const bool pulse = t % 2000 < 720 && t % 240 < 120;
      f.primary = f.secondary = pulse; f.color = scale({1, 0, 0}, pulse); break;
    }
    case State::Updating:
      f.primary = t % 400 < 200; f.secondary = !f.primary;
      f.color = scale({0.65, 0.04, 1}, 0.25 + 0.65 *
          (1 + std::sin(2 * pi * (t % 1600) / 1600.0 - 2 * pi * pixel / std::max(1, pixels))) / 2);
      break;
    case State::Shutdown: {
      const double fade = std::max(0.0, 1 - t / 2000.0);
      f.primary = f.secondary = t < 2000; f.color = scale({0.4, 0.4, 0.4}, fade); break;
    }
  }
  return f;
}

const char* name(State s) {
  switch (s) {
    case State::Starting: return "starting";
    case State::Waiting: return "waiting";
    case State::Operating: return "operating";
    case State::Warning: return "warning";
    case State::Fault: return "fault";
    case State::Updating: return "updating";
    case State::Shutdown: return "shutdown";
  }
  return "unknown";
}
}
