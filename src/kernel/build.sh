#!/bin/bash

# Following: https://wiki.ubuntu.com/Kernel/Dev/KernelModuleRebuild

# pre: kernel source installed...

usage() {
	echo "
Usage: $0 <options>
	Compile the midex module

Options:
	-d	install kernel sources and build dependencies
	-h	this help
	-i	install module in /lib/modules/`uname -r`
	-k	set the kernel source directory
	-u	uninstall module from /lib/modules/`uname -r`
"
}


MODULE_DIR=$PWD

DO_INSTALL=
DO_UNINSTALL=
DO_DEPENDENCIES=

while getopts ":dhik:u" opt; do
	case $opt in
		d)
			DO_DEPENDENCIES=yes
			;;
		h)
			usage;
			exit 0
			;;
		i)
			DO_INSTALL=yes
			;;
		k)
			KERNEL_SOURCE_DIR=$OPTARG
			;;
		u)
			DO_UNINSTALL=yes
			;;
		\?)
			echo "Invalid option: -$OPTARG" >&2
			exit 1
			;;
		:)
			echo "Option -$OPTARG requires an argument." >&2
			exit 1
		;;
	esac
done

##############################################
### Get sources and build dependencies
##############################################
if [ -n "$DO_DEPENDENCIES" ]
then
	echo "Installing needed kernel sources and build dependencies..."
	sudo apt-get source linux-image-$(uname -r)
	sudo apt-get build-dep linux-image-$(uname -r)
fi


source get_kernel_source_dir.sh


echo "Using kernel sources in $KERNEL_SOURCE_DIR"
echo ""
##############################################
### The actual compilation:
##############################################

cp -u /lib/modules/`uname -r`/build/.config $MODULE_DIR
cp -u /lib/modules/`uname -r`/build/Module.symvers $MODULE_DIR
cp -u /lib/modules/`uname -r`/build/Makefile $MODULE_DIR

pushd $KERNEL_SOURCE_DIR
make O=$MODULE_DIR outputmakefile
make O=$MODULE_DIR archprepare
make O=$MODULE_DIR prepare
make O=$MODULE_DIR modules SUBDIRS=scripts

if [ -e ./scripts/checkpatch.pl ]
then
	# some code (style) checking:
	echo "Running ./scripts/checkpatch.pl:"
	./scripts/checkpatch.pl -f $MODULE_DIR/sound/usb/midex/midex.c 
fi

# actual build:
make C=1 O=$MODULE_DIR modules SUBDIRS=$MODULE_DIR/sound/usb/midex/


##############################################
### install/remove module from /lib/modules
##############################################

if [ -n "$DO_UNINSTALL" ]
then
	echo "Removing snd-usb-midex.ko from /lib/modules/`uname -r`/kernel/sound/usb/midex/"
	sudo rm /lib/modules/`uname -r`/kernel/sound/usb/midex/snd-usb-midex.ko
fi

if [ -n "$DO_INSTALL" ]
then
	echo "Installing snd-usb-midex.ko in /lib/modules/`uname -r`/kernel/sound/usb/midex/"
	sudo mkdir -p /lib/modules/`uname -r`/kernel/sound/usb/midex/
	sudo cp $MODULE_DIR/sound/usb/midex/snd-usb-midex.ko /lib/modules/`uname -r`/kernel/sound/usb/midex/

	
	if [ -e /etc/modules-load.d/ ]
	then
		echo "Adding module to /etc/modules-load.d/ ..."
		sudo sh -c 'echo snd-usb-midex > /etc/modules-load.d/midex.conf'
	elif [ -e /etc/modules ]
	then
		echo "Adding module to /etc/modules ..."
		sudo sh -c 'echo snd-usb-midex >> /etc/modules'
	fi
fi

if [ -n "$DO_INSTALL" -o -n "$DO_UNINSTALL" ]
then
	echo "Running depmod..."
	sudo depmod
fi

if [ -n "$DO_INSTALL" ]
then
	echo ""
	echo "### Run the following to use the driver manually:"
	echo "sudo modprobe snd-usb-midex"
fi

