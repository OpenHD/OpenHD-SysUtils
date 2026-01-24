#ifndef SYSUTIL_VIDEO_H
#define SYSUTIL_VIDEO_H

#include <string>

namespace sysutil {

// Generates the decode script and systemd service file based on the detected platform.
// Returns true on success, false on failure.
bool generate_decode_scripts_and_services();

// Starts the default ground video pipeline when run_mode is "ground".
void start_ground_video_if_needed();

// Starts OpenHD services; starts QOpenHD in ground mode.
void start_openhd_services_if_needed();

// Returns true when the payload requests sysutils to handle video decode.
bool is_video_request(const std::string& line);
// Handles a video decode request and returns a JSON response.
std::string handle_video_request(const std::string& line);

}  // namespace sysutil

#endif  // SYSUTIL_VIDEO_H
