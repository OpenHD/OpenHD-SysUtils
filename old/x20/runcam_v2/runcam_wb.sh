#!/bin/bash

/usr/local/bin/x20/runcam_v2/get_cam_i2c.sh
CAM=$?
BUS=0

# RUNCAM_Read_Write(camera_device, 0x0001b8, 0x020b007b); # Manual
# RUNCAM_Read_Write(camera_device, 0x0001b8, 0x020b0079); # Auto

# It is possible to manually set the WB, but this isn't implemented yet.

if [ $1 -eq 0 ]; then
    # Set auto WB
     i2cset -y $BUS $CAM 0x12 0x00 0x01 0xb8 0x02 0x0b 0x00 0x79 i
elif [ $1 -eq 1 ]; then
    # Manual (fix at current value)
     i2cset -y $BUS $CAM 0x12 0x00 0x01 0xb8 0x02 0x0b 0x00 0x7b i
else
	echo "Valid values of white balance are: "
    echo "0 (auto) and 1 (fix to current)"
fi

