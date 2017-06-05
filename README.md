# Steinberg MIDEX8 driver for linux

This repository holds an (experimental) driver for the MIDEX8, both as kernel driver, and libusb.

There are still some issues with the kernel driver: see below.

in the 'doc' directory you will find some [analysis of the protocol](doc/analysis.md) in text and in wireshark files.

## Issues

So far I haven't found any issues on my real machine (with Ubuntu 16.04 and a 4.4.0 kernel).

The following issues seem to occur only when using VirtualBox (with Ubuntu 16.04, a 4.8.0 kernel and guest img installed) for testing:
* empty packet is seen on EP2(out) or EP4(out)... This causes the EP (or device?) to hang/block/seize
* There seems to be some urbs never completing (mostly timing and led command/reply) some time after this empty packet is sent. Currently this is resolved by unlinking the urb and trying again the next time.
* in firmware 0x1001 mode, the re-submission of a midi-in urb (EP2(in)) sometimes takes longer than 1ms

