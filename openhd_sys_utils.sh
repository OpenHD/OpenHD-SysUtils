#!/bin/bash

# This script handles initial configuation, updates and misc features which aren't included in the main OpenHD executable (yet)

# debug mode (shows journaldctl on the screen when logged in)
echo "OpenHD-Sys-Utils executed"

debug_file="/boot/openhd/debug.txt"
if [ -e "$debug_file" ]; then
    echo "debug mode selected"
    echo "sudo journalctl -f" >> /root/.bashrc
fi

# initialise functions
if [ -f "/boot/openhd/hardware_vtx_v20.txt" ]; then
 sudo bash /usr/local/bin/initX20.sh
 #rm /boot/openhd/hardware_vtx_v20.txt
fi

if [ -f "/boot/openhd/openhd/x86.txt" ]; then
 sudo mv /boot/openhd/openhd/* /boot/openhd
fi

if [[ "$(lsb_release -cs)" == "noble" ]]; then 
  if [ -f "/opt/setup" ]; then
  depmod -a
  rm /opt/setup
  sudo bash /usr/local/bin/openhd_resize_util.sh 404f7966-7c54-4170-8523-ed6a2a8da9bd 3
  reboot
  fi
fi

if [ -f "/boot/openhd/openhd/x86.txt" ]; then
 sudo mv /boot/openhd/openhd/* /boot/openhd
fi

if [ -f "/boot/openhd/x86.txt" ]; then
 sudo bash /usr/local/bin/initX86.sh
 touch /usr/local/share/executed
 rm /boot/openhd/x86.txt
fi

if [ -f "/boot/openhd/rock-5a.txt" ]; then
  sudo bash /usr/local/bin/initRock.sh
  rm /boot/openhd/rock-5a.txt
fi

if [ -f "/boot/openhd/rock-5b.txt" ]; then
  sudo bash /usr/local/bin/initRock.sh
  rm /boot/openhd/rock-5b.txt
fi

if [ -f "/boot/openhd/rock-rk3566.txt" ] || [ -f "/boot/openhd/openhd/rock-rk3566.txt" ]; then
  echo "detected rk3566 device"
  if  [ -e /dev/mmcblk1p1 ]; then
    if  [ -e /home/openhd/Videos ]; then
    mkdir -p /home/openhd/Videos_emmc
    mv /home/openhd/Videos /home/openhd/Videos_emmc
    rm -Rf /home/openhd/Videos
    fi
    mkdir -p /home/openhd/Videos
    sudo mount -t vfat /dev/mmcblk1p1 /home/openhd/Videos
    mv /home/openhd/Videos_emmc/* /home/openhd/Videos
  fi
  echo "run initRock"
  if  [ -e /config/write_logs.txt ]; then
  sudo journalctl > /config/logs.txt
  fi
  sudo bash /usr/local/bin/initRock.sh > /boot/debug.txt
  if [ -f "/boot/openhd/clearEMMC.txt" ] || [ -f "/home/openhd/Videos/clearEMMC.txt" ] ; then
    bash /usr/local/bin/openhd_emmc_util clear
    whiptail --msgbox "EMMC cleared Please reboot your system now" 10 40
  fi
fi

if [ -f "/boot/openhd/rpi.txt" ]; then
  if [ -f "/boot/openhd/air.txt" ]; then
  sudo bash /usr/local/bin/initPi.sh
  rm /boot/openhd/rpi.txt
  fi
fi
