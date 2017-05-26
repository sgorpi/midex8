# Steinberg MIDEX8 driver for linux

This repository holds an (experimental) driver for the MIDEX8, both as kernel driver, and libusb.

There are still some issues with the kernel driver: There seems to be some urbs never completing, and some empty urbs on the MIDI OUT port after sending...


in the 'doc' directory you will find some analysis of the protocol in text and in wireshark files.

