#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace sysutil::led {
enum class State { Starting, Waiting, Operating, Warning, Fault, Updating, Shutdown };
enum class Mode { Air, Ground, Record };
struct Color { double r = 0, g = 0, b = 0; };
struct Frame { double primary = 0, secondary = 0; Color color; };
struct View { State state; Mode mode; bool activity; };

// All times are monotonic milliseconds. Independent sources cannot clear one
// another's faults. Repeated notifications refresh expiry without restarting effects.
class Model {
 public:
  void status(const std::string& source, const std::string& state, int severity,
              bool error, std::uint64_t now, int ttl_ms = 3000);
  void runtime(Mode mode, bool operating, bool activity, bool recording,
               std::uint64_t now, int ttl_ms = 7000);
  View view(std::uint64_t now) const;
 private:
  struct Event { State state; std::uint64_t until; };
  std::map<std::string, Event> events_;
  State base_ = State::Starting;
  Mode mode_ = Mode::Air;
  bool operating_ = false, activity_ = false, recording_ = false;
  std::uint64_t runtime_until_ = 0;
};

Frame render(View view, std::uint64_t elapsed_ms, int pixel = 0, int pixels = 1);
const char* name(State state);
}  // namespace sysutil::led
