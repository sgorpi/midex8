#!/bin/bash

modprobe usbmon

sleep 0.5

setfacl -m u:hedde:r /dev/usbmon*

