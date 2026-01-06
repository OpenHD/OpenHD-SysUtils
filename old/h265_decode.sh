#!/bin/bash

#Simple decode script for OpenHD (h265)

function execute_kmsDecodeMPP {
    local plane=$1
    local mode=$2
    gst-launch-1.0 udpsrc port=5600 caps='application/x-rtp, payload=(int)96, clock-rate=(int)90000, media=(string)video, encoding-name=(string)H265' ! rtph265depay ! h265parse ! mppvideodec format=23 fast-mode=true ! queue ! kmssink plane-id="$plane" force-modesetting="$mode"
}
function execute_PeteVideoDecode {
    gst-launch-1.0 udpsrc port=5600 caps='application/x-rtp, payload=(int)96, clock-rate=(int)90000, media=(string)video, encoding-name=(string)H265' ! rtph265depay ! h265parse config-interval=1 ! video/x-h265,stream-format=byte-stream,alignment=au ! fdsink fd=1 | openhd_vid >/dev/tty1
}
function execute_FPVueVideoDecode {
gst-launch-1.0 udpsrc port=5600 caps='application/x-rtp, payload=(int)96, clock-rate=(int)90000, media=(string)video, encoding-name=(string)H265' ! rtph265depay ! h265parse config-interval=1 ! video/x-h265,stream-format=byte-stream,alignment=au ! fdsink fd=1 | fpvue  --rmode 10
}

function execute_FPVueVideoDecode2 {
  fpvue  --gst-udp-port 5600 --rmode 5 --h265
}

# Detect Platform
if [ -f "/boot/openhd/rock-rk3566.txt" ] || [ -f "/boot/openhd/openhd/rock-rk3566.txt" ]; then
#execute_PeteVideoDecode
execute_FPVueVideoDecode2
fi

if [ -e "/usr/local/share/openhd_platform/rock/rock5b" ]; then
execute_FPVueVideoDecode2
fi

if [ -e "/usr/local/share/openhd_platform/rock/rock5a" ]; then
execute_FPVueVideoDecode2
fi
