#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <fcntl.h>
#include <unistd.h>

namespace sysutil {

inline bool atomic_write(const std::filesystem::path& path,
                         const std::string& data) {
  auto temporary = path.string() + ".tmp.XXXXXX";
  const int fd = ::mkstemp(temporary.data());
  if (fd < 0) return false;

  std::size_t written = 0;
  while (written < data.size()) {
    const auto count = ::write(fd, data.data() + written, data.size() - written);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    written += count;
  }
  bool ok = written == data.size() && ::fsync(fd) == 0;
  if (::close(fd) != 0) ok = false;
  if (ok) ok = ::rename(temporary.c_str(), path.c_str()) == 0;
  if (!ok) {
    ::unlink(temporary.c_str());
    return false;
  }

  const int directory = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
  if (directory < 0) return false;
  ok = ::fsync(directory) == 0;
  ::close(directory);
  return ok;
}

}  // namespace sysutil
