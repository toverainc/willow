#!/bin/bash

ADF_PATH=/Users/kris/projects/willow/esp/esp-adf
PORT="/dev/cu.usbserial-31320"

case $1 in

config)
    idf.py menuconfig
;;

flash)
    idf.py -p "$PORT" flash monitor
;;

esac