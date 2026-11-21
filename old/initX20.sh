#!/bin/bash

#initialise x20 air-unit

#add platform identification
mkdir -p /usr/local/share/openhd_platform/x20
mkdir -p /usr/local/share/openhd_platform/wifi_card_type/88xxau/
touch /usr/local/share/openhd_platform/wifi_card_type/88xxau/custom
touch /usr/local/share/openhd_platform/x20/hdzero


echo 1 >/sys/class/leds/openhd-x20dev\:red\:usr/brightness
echo 0 >/sys/class/leds/openhd-x20dev\:green\:usr/brightness
echo 0 >/sys/class/leds/openhd-x20dev\:blue\:usr/brightness

depmod -a
modprobe HdZero
if [ ! -f /usr/local/share/openhd_platform/x20/firstboot_done ]; then
    echo "started once" >> /external/log2.txt
    touch /usr/local/share/openhd_platform/x20/firstboot_done
    # Storage provisioning is performed offline by ImageWriter.
else
    ls -a /usr/local/share/openhd_platform/x20/ >> /external/log2.txt
    echo "Already started before" >> /external/log2.txt
fi

echo 0 >/sys/class/leds/openhd-x20dev\:red\:usr/brightness
echo 0 >/sys/class/leds/openhd-x20dev\:green\:usr/brightness
echo 1 >/sys/class/leds/openhd-x20dev\:blue\:usr/brightness