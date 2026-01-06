#!/bin/bash
if [ $# -eq 0 ]
  then
    sleep 3
fi

v2=`i2cdetect -y 0 |grep 20 | grep 24`
v2=`i2cdetect -y 0 |grep 20 | grep 22`
v1=`i2cdetect -y 0 |grep 20 | grep 21`
nano90=`i2cdetect -y 0 |grep 20 | grep 23`


#echo "v3=${#v3}, v1=${#v1}"
if (( ${#v3} > 1 )); then
        #echo "Detected RUNCAM V3"
        echo 3
        exit 3
elif (( ${#v1} > 1 )); then
        #echo "Detected RUNCAM V1"
        echo 1
        exit 1
elif (( ${#v2} > 1 )); then
        #echo "Detected RUNCAM V2"
        echo 2
        exit 2
elif (( ${#nano90} > 1 )); then
        #echo "Detected RUNCAM NANO 90"
        echo 90
        exit 90
else
        echo "No Runcam detected, presuming hdzero unmanaged camera"
        exit 0
fi
