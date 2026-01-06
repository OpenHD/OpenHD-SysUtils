#!/bin/bash

TYPE=$1
COLOR=$2
MODIFIER=$3
DELAY=$4
DEBUG=$5

if [ "$TYPE" == "" ]; then 
    echo "OpenHD LED Control"
    echo "__________________"
    echo "usage:"
    echo "led TYPE COLOR DELAY DEBUG"
    echo "available types:"
    echo "________________"
    echo "on|off|toggle|manual|warning|error|flashing"
fi

# Kill all previous instances
pgrep -f led.sh | grep -v $$ | xargs --no-run-if-empty kill -9

debugMessage() {
    if [ "$DEBUG" == "debug" ]; then
        echo "$1"
    fi
}

# Detect platform and LED-type
if grep -q "Raspberry Pi" /proc/cpuinfo; then
    PLATFORM="pi"
    debugMessage "Platform: RPI"
    if [ -d /sys/class/leds/PWR ]; then
        PLATFORM="pi"
        debugMessage "Platform: RPI"
    fi
elif [ -d /sys/class/leds/openhd-x20dev ]; then
    PLATFORM="x20"
    FILE="/sys/class/leds/openhd-x20dev"
    debugMessage "Platform: X20"
elif [ -f /sys/class/leds/user-led2/brightness ]; then
    PLATFORM="rock5"
    FILE="/sys/class/leds/user-led2/brightness"
    debugMessage "Platform: Rock5"
elif [ -f /sys/class/leds/pi-led-green/brightness ]; then
    PLATFORM="cm3"
    FILE="/sys/class/leds/pi-led-green/brightness"
    debugMessage "Platform: CM3"
elif [ -f /sys/class/leds/board-led/brightness ]; then
    PLATFORM="zero3w"
    FILE="/sys/class/leds/board-led/brightness"
    debugMessage "Platform: Zero3w"
else
    echo "Platform not found"
fi

# Main functions
LED_ON() {
    if [ "$PLATFORM" == "x20" ]; then 
        if [ "$COLOR" == "red" ]; then 
            echo 1 > /sys/class/leds/openhd-x20dev:red:usr/brightness
            echo 0 > /sys/class/leds/openhd-x20dev:green:usr/brightness
            echo 0 > /sys/class/leds/openhd-x20dev:blue:usr/brightness
            debugMessage "Red LED on"
        elif [ "$COLOR" == "green" ]; then
            echo 0 > /sys/class/leds/openhd-x20dev:red:usr/brightness
            echo 1 > /sys/class/leds/openhd-x20dev:green:usr/brightness
            echo 0 > /sys/class/leds/openhd-x20dev:blue:usr/brightness
            debugMessage "Green LED on"            
        elif [ "$COLOR" == "blue" ]; then
            echo 0 > /sys/class/leds/openhd-x20dev:red:usr/brightness
            echo 0 > /sys/class/leds/openhd-x20dev:green:usr/brightness
            echo 1 > /sys/class/leds/openhd-x20dev:blue:usr/brightness
            debugMessage "Blue LED on"
        elif [ "$COLOR" == "cyan" ]; then
            echo 0 > /sys/class/leds/openhd-x20dev:red:usr/brightness
            echo 1 > /sys/class/leds/openhd-x20dev:green:usr/brightness
            echo 1 > /sys/class/leds/openhd-x20dev:blue:usr/brightness
            debugMessage "Cyan LED on"  
        elif [ "$COLOR" == "magenta" ]; then
            echo 1 > /sys/class/leds/openhd-x20dev:red:usr/brightness
            echo 0 > /sys/class/leds/openhd-x20dev:green:usr/brightness
            echo 1 > /sys/class/leds/openhd-x20dev:blue:usr/brightness
            debugMessage "Magenta LED on"  
        elif [ "$COLOR" == "yellow" ]; then
            echo 1 > /sys/class/leds/openhd-x20dev:red:usr/brightness
            echo 1 > /sys/class/leds/openhd-x20dev:green:usr/brightness
            echo 0 > /sys/class/leds/openhd-x20dev:blue:usr/brightness
            debugMessage "Yellow LED on"  
        elif [ "$COLOR" == "white" ]; then
            echo 1 > /sys/class/leds/openhd-x20dev:red:usr/brightness
            echo 1 > /sys/class/leds/openhd-x20dev:green:usr/brightness
            echo 1 > /sys/class/leds/openhd-x20dev:blue:usr/brightness
            debugMessage "White LED on"  
        else
            # Set it to white if no or wrong argument is passed
            echo 1 > /sys/class/leds/openhd-x20dev:red:usr/brightness
            echo 1 > /sys/class/leds/openhd-x20dev:green:usr/brightness
            echo 1 > /sys/class/leds/openhd-x20dev:blue:usr/brightness
            debugMessage "White LED on" 
        fi
    elif [ "$PLATFORM" == "pi" ]; then 
        if [ "$COLOR" == "green" ]; then 
            echo 1 > /sys/class/leds/ACT/brightness
            debugMessage "Green LED on" 
        elif [ "$COLOR" == "red" ]; then
            echo 1 > /sys/class/leds/PWR/brightness
            debugMessage "Red LED on" 
        else 
            echo 1 > /sys/class/leds/ACT/brightness
            echo 1 > /sys/class/leds/PWR/brightness
        fi
    elif [ "$PLATFORM" == "cm3" ]; then
        if [ "$COLOR" == "green" ]; then 
            echo 1 > /sys/class/leds/user-led/brightness
            debugMessage "Green LED on" 
        elif [ "$COLOR" == "green-pi" ]; then 
            echo 1 > /sys/class/leds/pi-led-green/brightness
            debugMessage "Green Pi LED on"
        elif [ "$COLOR" == "red" ]; then 
            echo 1 > /sys/class/leds/pwr-led-red/brightness
            debugMessage "Red LED on"
        else
            echo 1 > /sys/class/leds/user-led/brightness
            echo 1 > /sys/class/leds/pi-led-green/brightness
            echo 1 > /sys/class/leds/pwr-led-red/brightness
        fi
    elif [ "$PLATFORM" == "zero3w" ]; then
            echo 1 > /sys/class/leds/board-led/brightness
    elif [ "$PLATFORM" == "rock5" ]; then
            echo 1 > /sys/class/leds/user-led2/brightness
    fi
}

LED_OFF() {
    if [ "$PLATFORM" == "x20" ]; then 
        if [ "$COLOR" == "red" ]; then 
            echo 0 > /sys/class/leds/openhd-x20dev:red:usr/brightness
            debugMessage "Red LED off"
        elif [ "$COLOR" == "green" ]; then
            echo 0 > /sys/class/leds/openhd-x20dev:green:usr/brightness
            debugMessage "Green LED off"            
        elif [ "$COLOR" == "blue" ]; then
            echo 0 > /sys/class/leds/openhd-x20dev:blue:usr/brightness
            debugMessage "Blue LED off"
        elif [ "$COLOR" == "cyan" ]; then
            echo 0 > /sys/class/leds/openhd-x20dev:green:usr/brightness
            echo 0 > /sys/class/leds/openhd-x20dev:blue:usr/brightness
            debugMessage "Cyan LED off"  
        elif [ "$COLOR" == "magenta" ]; then
            echo 0 > /sys/class/leds/openhd-x20dev:red:usr/brightness
            echo 0 > /sys/class/leds/openhd-x20dev:blue:usr/brightness
            debugMessage "Magenta LED off"  
        elif [ "$COLOR" == "yellow" ]; then
            echo 0 > /sys/class/leds/openhd-x20dev:red:usr/brightness
            echo 0 > /sys/class/leds/openhd-x20dev:green:usr/brightness
            debugMessage "Yellow LED off"  
        elif [ "$COLOR" == "white" ]; then
            echo 0 > /sys/class/leds/openhd-x20dev:red:usr/brightness
            echo 0 > /sys/class/leds/openhd-x20dev:green:usr/brightness
            echo 0 > /sys/class/leds/openhd-x20dev:blue:usr/brightness
            debugMessage "White LED off"  
        else
            # Set it to white if no or wrong argument is passed
            echo 0 > /sys/class/leds/openhd-x20dev:red:usr/brightness
            echo 0 > /sys/class/leds/openhd-x20dev:green:usr/brightness
            echo 0 > /sys/class/leds/openhd-x20dev:blue:usr/brightness
            debugMessage "White LED off" 
        fi
    elif [ "$PLATFORM" == "pi" ]; then
        if [ "$COLOR" == "green" ]; then 
            echo 0 > /sys/class/leds/ACT/brightness
            debugMessage "Green LED off" 
        elif [ "$COLOR" == "red" ]; then
            echo 0 > /sys/class/leds/PWR/brightness
            debugMessage "Red LED off" 
        else 
            echo 0 > /sys/class/leds/ACT/brightness
            echo 0 > /sys/class/leds/PWR/brightness
        fi
    elif [ "$PLATFORM" == "cm3" ]; then
        if [ "$COLOR" == "green" ]; then 
            echo 0 > /sys/class/leds/user-led/brightness
            debugMessage "Green LED off" 
        elif [ "$COLOR" == "green-pi" ]; then 
            echo 0 > /sys/class/leds/pi-led-green/brightness
            debugMessage "Green Pi LED off"
        elif [ "$COLOR" == "red" ]; then 
            echo 0 > /sys/class/leds/pwr-led-red/brightness
            debugMessage "Red LED off"
        else
            echo 0 > /sys/class/leds/user-led/brightness
            echo 0 > /sys/class/leds/pi-led-green/brightness
            echo 0 > /sys/class/leds/pwr-led-red/brightness
        fi
    elif [ "$PLATFORM" == "zero3w" ]; then
            echo 0 > /sys/class/leds/board-led/brightness
    elif [ "$PLATFORM" == "rock5" ]; then
            echo 0 > /sys/class/leds/user-led2/brightness
    fi
}

BLINK_LED() {
    while true; do
       LED_ON
       sleep $(echo "$MODIFIER * 0.01" | bc)
       LED_OFF
       sleep $(echo "$MODIFIER * 0.01" | bc)
    done
}
BLINK_LED_ASYNC() {
    while true; do
       LED_ON
       sleep $(echo "$DELAY1 * 0.01" | bc)
       LED_OFF
       sleep $(echo "$DELAY2 * 0.01" | bc)
    done
}
BLINK_LED_ASYNC_MULTI() {
    while true; do
       COLOR=$COLOR1
       LED_ON
       sleep $(echo "$DELAY1 * 0.01" | bc)
       LED_OFF
       sleep $(echo "$DELAY2 * 0.01" | bc)
       COLOR=$COLOR2
       LED_ON
       sleep $(echo "$DELAY1 * 0.01" | bc)
       LED_OFF
       sleep $(echo "$DELAY2 * 0.01" | bc)
    done
}
BLINK_LED_ASYNC_MULTI_2() {
    while true; do
       COLOR=$COLOR1
       LED_ON
       sleep $(echo "$DELAY1 * 0.01" | bc)
       COLOR=$COLOR2
       LED_ON
       sleep $(echo "$DELAY1 * 0.01" | bc)
       COLOR=$COLOR1
       LED_OFF
       sleep $(echo "$DELAY2 * 0.01" | bc)
       COLOR=$COLOR2
       LED_OFF
       sleep $(echo "$DELAY2 * 0.01" | bc)
    done
}
BLINK_LED_ASYNC_MULTI_SWITCHING() {
    while true; do
       COLOR=$COLOR1
       LED_ON
       COLOR=$COLOR2
       LED_OFF
       sleep $(echo "$DELAY1 * 0.01" | bc)
       COLOR=$COLOR2
       LED_ON
       COLOR=$COLOR1
       LED_OFF
       sleep $(echo "$DELAY2 * 0.01" | bc)
    done
}

# LED mode Selection
if [ "$TYPE" == "on" ]; then 
    LED_ON
elif [ "$TYPE" == "off" ]; then 
    LED_OFF
elif [ "$TYPE" == "toggle" ]; then 
    if [[ $(cat $FILE) == "0" ]]; then
    LED_ON
    else
    LED_OFF
    fi
elif [ "$TYPE" == "manual" ]; then 
    if [ -z "$MODIFIER" ]; then
        echo "Missing delay value for MANUAL mode."
    else
        BLINK_LED
    fi
elif [ "$TYPE" == "warning" ]; then 
    if [ -z "$MODIFIER" ]; then
        echo "Missing value for Warning mode."
    else
        COLOR="green"
        DELAY1="50"
        DELAY2="200"
        BLINK_LED_ASYNC
        debugMessage "LED Warning $MODIFIER" 
    fi
elif [ "$TYPE" == "error" ]; then 
    if [ -z "$MODIFIER" ]; then
        echo "Missing value for Error mode."
    else
        DELAY1="50"
        DELAY2="100"
        COLOR1="green"
        COLOR2="red"
        BLINK_LED_ASYNC_MULTI_2
        debugMessage "LED Error $MODIFIER" 
    fi
elif [ "$TYPE" == "flashing" ]; then 
        DELAY1="1"
        DELAY2="1"
        COLOR1="green"
        COLOR2="blue"
        BLINK_LED_ASYNC_MULTI_SWITCHING
        debugMessage "LED $MODIFIER" 
fi
