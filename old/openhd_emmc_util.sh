#!/bin/bash

# This script handles the EMMC

COMMAND=$1
DEBUG=$2

BOARD=$(tr -d '\0' </proc/device-tree/model)
SDCARD="N/A"
EMMC="N/A"

# Set window/text color
export NEWT_COLORS='
root=,black
window=black,black
border=black,black
textbox=white,black
button=white,black
emptyscale=,black
fullscale=,white
'

debugMessage() {
    if [ "$DEBUG" == "debug" ]; then
        [ -d /boot/openhd ] || mkdir -p /boot/openhd && touch /boot/openhd/emmc_tool.log
        echo "$(date '+%Y-%m-%d %H:%M:%S') $1" 
        echo "$(date '+%Y-%m-%d %H:%M:%S') $1" >> /boot/openhd/emmc_tool.log
    fi
}

if [ "$BOARD" == "Radxa CM3 RPI CM4 IO" ]; then
    echo "CM3"
    EMMC=/dev/mmcblk0
    SDCARD=/dev/mmcblk1
elif [ "$BOARD" == "Radxa ZERO 3" ]; then
    echo "Zero3"
    EMMC=/dev/mmcblk0
    SDCARD=/dev/mmcblk1
elif [ "$BOARD" == "Radxa ROCK 5B" ]; then
    echo "Rock5B"
    EMMC=/dev/mmcblk3
    SDCARD=/dev/mmcblk4
elif [ "$BOARD" == "Radxa ROCK 5A" ]; then
    echo "Rock5A"
    EMMC=/dev/mmcblk4
    SDCARD=/dev/mmcblk9
elif [ "$BOARD" == "CM5 RPI CM4 IO" ]; then
    echo "CM5"
    EMMC=/dev/mmcblk4
    SDCARD=/dev/mmcblk2
elif [ "$BOARD" == "OpenHD X20 Dev" ]; then
    echo "X20"
    EMMC=/dev/mmcblk1
    SDCARD=/dev/mmcblk0
else
    echo "Unsupported board: $BOARD"
fi

flash_emmc() {
    if [ -f /opt/additionalFiles/emmc.img ]; then
    FILESIZE=$(stat -c "%s" /opt/additionalFiles/emmc.img)
    debugMessage "$FILESIZE image is being flashed!"
    (pv -n /opt/additionalFiles/emmc.img | dd of="$EMMC" bs=128M conv=notrunc,noerror) 2>&1 | whiptail --gauge "Flashing OpenHD to EMMC, please wait..." 10 70 0
    debugMessage "Flash completed!"
    mkdir -p /media/new
    mount "$EMMC"p1 /media/new
    cp -r /boot/openhd/* /media/new/openhd/
    debugMessage "Copied openhd config files!"
    /usr/local/bin/led_sys.sh off
    rm -Rf /etc/profile
    reboot
else
    debugMessage "Failed emmc.img not found"
    /usr/local/bin/led_sys.sh off
    debugMessage "LED off"
    exit 1
fi
}

#Main
echo "EMMC: $EMMC"
echo "SDCARD: $SDCARD"

/usr/local/bin/led_sys.sh off
if [ "$COMMAND" == "clear" ]; then
    /usr/local/bin/led_sys.sh flashing blueANDgreen 2 &
    sudo dd if=/dev/zero of=$EMMC bs=512 count=1 seek=1
    /usr/local/bin/led_sys.sh off

elif [ "$COMMAND" == "flash" ]; then
    /usr/local/bin/led_sys.sh flashing blueANDgreen 2 &
    flash_emmc
    /usr/local/bin/led_sys.sh off

else
    echo "Unsupported command"
fi