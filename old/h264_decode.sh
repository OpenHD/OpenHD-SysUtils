#!/bin/bash

#Simple decode script for OpenHD (h264)

function execute_kmsDecodeMPP {
    local plane=$1
    local mode=$2
    gst-launch-1.0 udpsrc port=5600 caps='application/x-rtp, payload=(int)96, clock-rate=(int)90000, media=(string)video, encoding-name=(string)H264' ! rtph264depay ! h264parse ! mppvideodec format=23 fast-mode=true ! queue ! kmssink plane-id="$plane" force-modesetting="$mode"
}
function execute_fpvVideoDecode {
    gst-launch-1.0 udpsrc port=5600 caps='application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264' ! rtph264depay ! 'video/x-h264,stream-format=byte-stream' ! fdsink | fpv_video0.bin /dev/stdin
}
function execute_PeteVideoDecode {
    gst-launch-1.0 udpsrc port=5600 caps='application/x-rtp, payload=(int)96, clock-rate=(int)90000, media=(string)video, encoding-name=(string)H264' ! rtph264depay ! h264parse config-interval=1 ! video/x-h264,stream-format=byte-stream,alignment=au ! fdsink fd=1 | openhd_vid >/dev/tty1
}
function execute_FPVueVideoDecode {
    gst-launch-1.0 udpsrc port=5600 caps='application/x-rtp, payload=(int)96, clock-rate=(int)90000, media=(string)video, encoding-name=(string)H264' ! rtph264depay ! h264parse config-interval=1 ! video/x-h264,stream-format=byte-stream,alignment=au ! fdsink fd=1 | fpvue  --rmode 10
}
function execute_FPVueVideoDecode2 {
  fpvue  --gst-udp-port 5600 --rmode 5 --x20-auto
}

# Detect Platform
if grep -q "Raspberry Pi" /proc/device-tree/model; then
execute_fpvVideoDecode
fi

if [ -f "/config/openhd/rock-rk3566.txt" ]; then
#execute_PeteVideoDecode
#execute_FPVueVideoDecode
execute_FPVueVideoDecode2
fi

if [ -e "/usr/local/share/openhd_platform/rock/rock5b" ]; then
execute_FPVueVideoDecode2
fi

if [ -e "/usr/local/share/openhd_platform/rock/rock5a" ]; then
execute_FPVueVideoDecode2
fi
