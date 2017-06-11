# Steinberg MIDEX8 driver for linux

This repository holds an (experimental) driver for the Steinberg MIDEX8, both as linux kernel driver, using ALSA, and as libusb demo application. The ALSA driver exposes the different physical ports as raw midi subdevices.

In the [src/kernel/](source/kernel/) directory you will find `build.sh`, a script to build the module. The -h option for help.

First, you need to install your current kernel's sources and build dependencies using, e.g.:
```
sudo apt-get source linux-image-$(uname -r)
sudo apt-get build-dep linux-image-$(uname -r)
```
Then, use the build script to execute the commands needed to build the module (following this guide: https://wiki.ubuntu.com/Kernel/Dev/KernelModuleRebuild ).

After building, load it with insmod, or place it in /lib/modules/\`uname -r\`/kernel/sound/usb/midex/ and run `sudo depmod` afterwards.


In the 'doc' directory you will find some [analysis of the protocol](doc/analysis.md) in text and in wireshark files.

If you have a MIDEX3, I would love to get some USB raw packets monitoring / pcap files from you, to see if this driver could also be used for that.

## Issues

#### Normal machine

So far I haven't found any issues on my real machine (with Ubuntu 16.04 and a 4.4.0 kernel).

#### Virtualbox

The following issues seem to occur only when using VirtualBox (with Ubuntu 16.04, a 4.8.0 kernel and guest img installed) for testing:
* empty packet is seen on EP2(out) or EP4(out)... This causes the EP (or device?) to hang/block/seize
* There seems to be some urbs never completing (mostly timing and led command/reply) some time after this empty packet is sent. Currently this is resolved by unlinking the urb and trying again the next time.
* in firmware 0x1001 mode, the re-submission of a midi-in urb (EP2(in)) sometimes takes longer than 1ms

