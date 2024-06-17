#!/bin/bash

#add platform identification
mkdir -p /usr/local/share/openhd/platform/rock


if [[ -f "/boot/openhd/rock-5a.txt" ]]; then
    echo "Running on a rock5 A"
    sudo systemctl enable ssh
    sleep 5
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
    sleep 5
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

if [[ -f "/boot/openhd/rock-rk3566.txt" ]]; then
    echo "Running on a rk3566 "

    if [ -e /boot/openhd/openhd ]; then
        #Annoying hack to get files from mounted partition 
        mkdir /boot/temp
        mv /boot/openhd/openhd/* /boot/temp
        rm -rf /boot/openhd/openhd
        mv /boot/temp/* /boot/openhd/
        rm -Rf /boot/temp
        sleep 1
    fi

    if [ -e /boot/openhd/air.txt ]; then 
        if [ -e /boot/openhd/camera1.txt ] && [ ! -e /boot/openhd/camera.txt ]; then
            #quite hacky now, but better then nothing
            sudo systemctl stop h264_decode
            sudo systemctl disable h264_decode
            sudo systemctl stop openhd
            sudo systemctl stop qopenhd
            sudo systemctl disable qopenhd
            touch /boot/openhd/camera.txt
            bash /usr/local/bin/ohd_camera_setup.sh > /boot/openhd/camera.txt
            sleep 2
            bash /usr/local/bin/ohd_camera_setup.sh > /boot/openhd/camera.txt
            reboot
        fi
    exit 0
    fi
    # config_file=$(find /boot/openhd/ -type f -name 'IMX*')
    
    # if [[ -n "$config_file" ]]; then
    #     if [[ "$config_file" == *"/708"* ]]; then
    #         cp "/boot/openhd/rock3_camera_configs/zero3w/imx708.conf" "/boot/extlinux/extlinux.conf"
    #         echo "$config_file written"
    #     elif [[ "$config_file" == *"/IMX462"* ]]; then
    #         cp "/boot/openhd/rock3_camera_configs/zero3w/imx462.conf" "/boot/extlinux/extlinux.conf"
    #         echo "$config_file written"
    #     else
    #         echo "No Camera configured"
    #     fi
    # else
    #     echo "Config file not found"
    # fi

    if [[ -e "/boot/openhd/resize.txt" ]]; then
    echo "resizing started"
    rm /boot/openhd/resize.txt
    fi

fi