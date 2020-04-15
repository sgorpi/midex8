#!/bin/bash

#
MODULE_DIR=$PWD
KERNEL_VERSION=`uname -r | sed 's/-.\+//'`

source get_kernel_source_dir.sh

rm -f $MODULE_DIR/.config
rm -f $MODULE_DIR/Module.symvers
rm -f $MODULE_DIR/Makefile

rm -rf $MODULE_DIR/arch/
rm -rf $MODULE_DIR/include/
rm -rf $MODULE_DIR/kernel/
rm -rf $MODULE_DIR/scripts/

rm -f $MODULE_DIR/source

rm -f $MODULE_DIR/sound/usb/midex/*.o*
rm -f $MODULE_DIR/sound/usb/midex/*.ko
rm -f $MODULE_DIR/sound/usb/midex/*.mod.c
rm -f $MODULE_DIR/sound/usb/midex/Module.symvers
rm -f $MODULE_DIR/sound/usb/midex/modules.order
