#!/bin/bash

insmod sound/usb/midex/snd-usb-midex.ko

echo "Press enter to unload"
read USER_INPUT

rmmod sound/usb/midex/snd-usb-midex.ko


