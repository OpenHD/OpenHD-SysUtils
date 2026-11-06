#pragma once
#include <filesystem>
#include <utility>
#include <fcntl.h>
#include <unistd.h>

namespace sysutil {
// Only the operation that acquired a hold may release it. Scope cleanup also
// covers every failed formatting/resizing step, so OpenHD can start afterwards.
class ScopedStartupHold {
 public:
  explicit ScopedStartupHold(std::filesystem::path path) : path_(std::move(path)) {
    const int fd = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    acquired_ = fd >= 0;
    if (fd >= 0) ::close(fd);
  }
  ~ScopedStartupHold() {
    if (acquired_) { std::error_code ec; std::filesystem::remove(path_, ec); }
  }
  ScopedStartupHold(const ScopedStartupHold&) = delete;
  ScopedStartupHold& operator=(const ScopedStartupHold&) = delete;
  bool acquired() const { return acquired_; }
 private:
  std::filesystem::path path_;
  bool acquired_ = false;
};
}
