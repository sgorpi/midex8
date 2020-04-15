#!/bin/bash

# Following: https://wiki.ubuntu.com/Kernel/Dev/KernelModuleRebuild

#TODO: https://askubuntu.com/questions/1083091/how-to-compile-linux-kernel-module-printk-missing

# pre: kernel source installed...

usage() {
	echo "
Usage: $0 <options>
	Compile the midex module

Options:
	-c  run checkpatch.pl on source file for code style checks
	-d	install kernel sources and build dependencies
	-h	this help
	-i	install module in /lib/modules/`uname -r`
	-k	set the kernel source directory if autodetect doesn't find it
	-u	uninstall module from /lib/modules/`uname -r`
"
}


MODULE_DIR=$PWD

DO_CHECKPATCH=
DO_INSTALL=
DO_UNINSTALL=
DO_DEPENDENCIES=

while getopts ":cdhik:u" opt; do
	case $opt in
		c)
			DO_CHECKPATCH=yes
			;;
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

source get_kernel_source_dir.sh


echo "Using kernel sources in $KERNEL_SOURCE_DIR"
echo ""

##############################################
### Get sources and build dependencies
##############################################
if [ -n "$DO_DEPENDENCIES" ]
then
	echo "Installing needed kernel headers and build dependencies..."
	
	if [ -z $KERNEL_HEADERS ]
	then
		KERNEL_HEADERS=/usr/src/linux-headers-$(uname -r)
	fi

	if [ ! -e $KERNEL_HEADERS ]
	then
		# first try generic headers
		sudo apt-get linux-headers-generic
	fi
	if [ ! -e $KERNEL_HEADERS ]
	then
		# then try specific headers
		sudo apt-get linux-headers-$(uname -r)
	fi
	
	# and the build dependencies for the current linux kernel
	sudo apt-get build-dep linux-image-$(uname -r)
fi


##############################################
### The actual compilation:
##############################################


if [ -n "$DO_CHECKPATCH" -a -e $KERNEL_SOURCE_DIR/scripts/checkpatch.pl ]
then
	# some code (style) checking:
	echo "Running $KERNEL_SOURCE_DIR/scripts/checkpatch.pl:"
	$KERNEL_SOURCE_DIR/scripts/checkpatch.pl --root=$KERNEL_SOURCE_DIR -f $MODULE_DIR/sound/usb/midex/midex.c
	# will give an error if only linux headers are used
fi

# actual build:
make -C $KERNEL_SOURCE_DIR M=$MODULE_DIR/sound/usb/midex/

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

echo ""
echo "### Run the following to use the driver manually:"
if [ -n "$DO_INSTALL" ]
then
	echo "sudo modprobe snd-usb-midex"
else
	echo "insmod $MODULE_DIR/sound/usb/midex/snd-usb-midex.ko"
fi

