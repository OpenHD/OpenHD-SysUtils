#ifndef SYSUTIL_VIDEO_H
#define SYSUTIL_VIDEO_H

namespace sysutil {

// Generates the decode script and systemd service file based on the detected platform.
// Returns true on success, false on failure.
bool generate_decode_scripts_and_services();

}  // namespace sysutil

#endif  // SYSUTIL_VIDEO_H
