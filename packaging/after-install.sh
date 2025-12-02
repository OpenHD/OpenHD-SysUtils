#!/bin/bash

cp /usr/local/share/openhd_misc/motd /etc/motd
cat /etc/motd

if [ -d /home/pi ]; then
    echo "activating autologin on raspberry pi"
    sudo sed -i 's/^ExecStart=-\/sbin\/agetty .*$/ExecStart=-\/sbin\/agetty --skip-login --login-options "-f root" %I 38400 linux/' /usr/lib/systemd/system/getty@.service
fi

if [ ! -e "/usr/lib/modules/5.8.0/kernel/drivers/media/platform/sunxi" ]; then
    echo "Non Custom Installation Found"
    rm -Rf /usr/local/bin/x20/
    rm -Rf /etc/systemd/system/temperature_guardian.service
    #uncommented, otherwise the package install will fail and break things
    #systemctl disable temperature_guardian.service
else
 echo "You're using OpenHD Hardware, thank you"
fi
