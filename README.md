# Steinberg MIDEX8 driver for linux

This repository holds an (experimental) driver for the MIDEX8, both as kernel driver, and libusb.

There are still some issues with the kernel driver: see below.

in the 'doc' directory you will find some [analysis of the protocol](doc/analysis.md) in text and in wireshark files.

## Issues

* There seems to be some urbs never completing (mostly timing and led command/reply). Currently this is resolved by unlinking the urb and trying again the next time.
* empty packet is seen on EP2(out) or EP4(out)... This causes the EP (or device?) to hang/block/seize
* in firmware 0x1001 mode, the re-submission of a midi-in urb (EP2(in)) sometimes takes longer than 1ms
* in firmware 0x1001 mode, sporadically the input blocks until a midi out message is sent.

