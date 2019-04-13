#!/bin/bash

KERNEL_VERSION=`uname -r | sed 's/-.\+//'`
echo "Version: $KERNEL_VERSION"

##############################################
### Try to establish where the kernel sources are
##############################################
# first locally:
if [ ! -e "$KERNEL_SOURCE_DIR" ]
then
	KERNEL_SOURCE_DIR=`find $PWD -maxdepth 1 -type d -name "linux-*"`
fi

# if not existing, then maybe it is a clone from an ubuntu repo
if [ ! -e "$KERNEL_SOURCE_DIR" ]
then
	KERNEL_SOURCE_DIR=`find $PWD -maxdepth 1 -type d -name "ubuntu-*"`
fi

# or the sources installed in a package (try to match uname -r)
if [ ! -e "$KERNEL_SOURCE_DIR" ]
then
	KERNEL_SOURCE_DIR=`find /usr/src/ -maxdepth 1 -type d -name "linux-$KERNEL_VERSION*"`
fi

# or the sources installed in a package (try to match uname -r)
if [ ! -e "$KERNEL_SOURCE_DIR" ]
then
	KERNEL_SOURCE_DIR=`find /usr/src/ -maxdepth 1 -type d -name "linux-source-$KERNEL_VERSION*"`
fi

# check if we asked the user before:
if [ ! -e "$KERNEL_SOURCE_DIR" ]
then
	if [ -e "build.sh.cfg" ]
	then
		KERNEL_SOURCE_DIR=`cat build.sh.cfg`
	fi
fi	
# and if we still don't know, ask the user
if [ ! -e "$KERNEL_SOURCE_DIR" ]
then
	echo "Where are the kernel sources located?"
	read KERNEL_SOURCE_DIR
	echo "$KERNEL_SOURCE_DIR" > build.sh.cfg
fi

