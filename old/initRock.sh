#!/bin/bash

#add platform identification
mkdir -p /usr/local/share/openhd/platform/rock


if [[ -f "/boot/openhd/rock-5a.txt" ]]; then
    echo "Running on a rock5 A"
    sudo systemctl enable ssh
    sudo systemctl start ssh
    mkdir -p /usr/local/share/openhd/platform/rock/rock5a
    config_file=$(find /boot/openhd/ -type f -name 'IMX*')
    
    if [[ -n "$config_file" ]]; then
        if [[ "$config_file" == *"/IMX415"* ]]; then
            cp "/boot/openhd/rock5_camera_configs/a/imx415.conf" "/boot/extlinux/extlinux.conf"
            echo "$config_file written"
        elif [[ "$config_file" == *"/IMX462"* ]]; then
            cp "/boot/openhd/rock5_camera_configs/a/imx462.conf" "/boot/extlinux/extlinux.conf"
            echo "$config_file written"
        else
            echo "No Camera configured"
        fi
    else
        echo "Config file not found"
    fi
fi


if [[ -f "/boot/openhd/rock-5b.txt" ]]; then
    echo "Running on a rock5 B"
    mkdir -p /usr/local/share/openhd/platform/rock/rock5b
    sudo systemctl enable ssh
    sudo systemctl start ssh
    config_file=$(find /boot/openhd/ -type f -name 'IMX*')
    
    if [[ -n "$config_file" ]]; then
        if [[ "$config_file" == *"/IMX415"* ]]; then
            cp "/boot/openhd/rock5_camera_configs/b/imx415.conf" "/boot/extlinux/extlinux.conf"
            echo "$config_file written"
        elif [[ "$config_file" == *"/IMX462"* ]]; then
            cp "/boot/openhd/rock5_camera_configs/b/imx462.conf" "/boot/extlinux/extlinux.conf"
            echo "$config_file written"
        else
            echo "No Camera configured"
        fi
    else
        echo "Config file not found"
    fi
fi

if [[ -f "/config/openhd/rock-rk3566.txt" ]]; then
    echo "Running on a rk3566 "
        if [[ -f "/config/openhd/resize.txt" ]]; then
        mkdir -p /run/openhd/
        touch /run/openhd/hold.pid
        echo resizing partition
        parted /dev/mmcblk1 --script resizepart 4 100%
        sudo rm /config/openhd/resize.txt
        sudo mkfs.vfat -F 32 -n "RECORDINGS" /dev/mmcblk1p4
        reboot
        fi
    if [ -e /config/openhd/air.txt ]; then 
        if [ -e /config/openhd/camera1.txt ] && [ ! -e /config/openhd/camera.txt ]; then
            #quite hacky now, but better then nothing
            sudo systemctl stop h264_decode
            sudo systemctl disable h264_decode
            sudo systemctl stop openhd
            sudo systemctl stop qopenhd
            sudo systemctl disable qopenhd
            touch /config/openhd/camera.txt
            bash /usr/local/bin/ohd_camera_setup.sh > /config/openhd/camera.txt
            sleep 2
            bash /usr/local/bin/ohd_camera_setup.sh > /config/openhd/camera.txt
            reboot
        fi
    exit 0
    fi
   
    if [[ -e "/boot/openhd/resize.txt" ]]; then
    echo "resizing started"
    rm /boot/openhd/resize.txt
    fi

fi