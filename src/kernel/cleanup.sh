#!/bin/bash

#
KERNEL_SOURCE_DIR=/home/hedde/Projects/ubuntu-xenial
MODULE_DIR=/home/hedde/Projects/midex/src/kernel/

rm -f $MODULE_DIR/.config
rm -f $MODULE_DIR/Module.symvers
rm -f $MODULE_DIR/Makefile

rm -rf $MODULE_DIR/arch/
rm -rf $MODULE_DIR/include/
rm -rf $MODULE_DIR/kernel/
rm -rf $MODULE_DIR/scripts/

rm -f $MODULE_DIR/source


