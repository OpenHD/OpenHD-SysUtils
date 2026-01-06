#!/bin/bash

# Runcam v3 : 0x24, v2: 0x22, v1:0x21, nano 90: 0x23

/usr/local/bin/x20/runcam_v2/detect_runcam.sh 1
CAM=$?
echo "Detected CAM $CAM, finding I2C address"
if [ $CAM -eq 1 ]; then
	exit 33
elif [ $CAM -eq 2 ]; then
    exit 34
elif [ $CAM -eq 3 ]; then
    exit 36
elif [ $CAM -eq 90 ]; then
    exit 35
fi
	
exit 0 # No runcam