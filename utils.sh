#!/bin/bash
set -e # bail on error

ADF_VER="v2.4.1"
PLATFORM="esp32s3" # Current general family

export ADF_PATH="$PWD/deps/esp-adf"
export IDF_PATH="$ADF_PATH/esp-idf"

check_port() {
# TODO: Figure out how to do this cross-platform or read a config or something
export PORT="/dev/cu.usbserial-31320"

if [ ! -c $PORT ]; then
    echo "Cannot find configured port $PORT - exiting"
    exit 1
fi
}

# Pull in environment
if [ -r $IDF_PATH/export.sh ]; then
    . $IDF_PATH/export.sh > /dev/null
else
    echo "Environment not found - normal for setup" 
fi

print_monitor_help() {
echo "
You can exit the serial monitor with CTRL + ]
"
}

case $1 in

config)
    idf.py menuconfig
;;

clean)
    idf.py fullclean
;;

build)
    idf.py build
;;

flash)
    check_port
    print_monitor_help
    idf.py -p "$PORT" flash monitor
;;

monitor)
    check_port
    print_monitor_help
    idf.py -p "$PORT" monitor
;;

destroy)
    echo "YOU ARE ABOUT TO REMOVE THIS ENTIRE ENVIRONMENT AND RESET THE REPO. HIT ENTER TO CONFIRM."
    read
    echo "SERIOUSLY - YOU WILL LOSE WORK AND I WILL NOT STOP YOU IF YOU HIT ENTER AGAIN!"
    read
    echo "LAST CHANCE!"
    read
    git reset --hard
    git clean -fdx
    rm -rf ~/.espressif deps
    echo "Not a trace left. You will have to run setup again."
;;

setup)
    mkdir -p deps
    cd deps
    # Setup ADF
    git clone --recursive -b "$ADF_VER" https://github.com/espressif/esp-adf.git
    cd esp-adf/esp-idf
    ./install.sh "$PLATFORM"

    # This is ridiculous
    cd $IDF_PATH
    git apply $ADF_PATH/idf_patches/idf_v4.4_freertos.patch

    cp sdkconfig.sallow sdkconfig

    # Configure WiFi

    #echo "Please enter the SSID you would like to connect to"
    #read SSID

    #echo "Please enter the passphrase for SSID $SSID"
    #read KEY

    #sed -i 's/old-text/new-text/g' input.txt
    #sed -i 's/old-text/new-text/g' input.txt
;;

*)
    echo "Passing args to idf.py as-is"
    shift
    idf.py "$@"
;;

esac