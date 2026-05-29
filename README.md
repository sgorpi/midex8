# Steinberg MIDEX8 driver for linux

This repository holds an (experimental) driver for the Steinberg MIDEX8, both as linux kernel driver, using ALSA, and as libusb demo application. The ALSA driver exposes the different physical ports as raw midi subdevices.

In the [src/kernel/](src/kernel/) directory you will find `build.sh`, a script to build the module. The -h option for help.

First, you need to install your current kernel's headers and build essentials using, e.g.:
```
sudo apt-get linux-headers-$(uname -r)
sudo apt-get build-essential
```
Then, use the build script to execute the commands needed to build the module (following this guide: https://wiki.ubuntu.com/Kernel/Dev/KernelModuleRebuild ).

After building, load it with insmod, or install it (and the firmware blobs, see below) with `./build.sh -i` followed by `sudo depmod`.

## Firmware upload

The MIDEX devices (except MIDEX8 r2) ship with no firmware in their EZ-USB microcontroller and
enumerate under a "loader" USB product ID. The driver now uploads the
operational firmware itself, after which the device renumerates to its
operational PID and the normal MIDI driver path takes over. The supported
transitions are:

| Loader PID | Device                     | Operational PID |
| ---------- | -------------------------- | --------------- |
| `0x1000`   | MIDEX8 r1 (EZ-USB AN2131)  | `0x1001`        |
| `0x1010`   | MIDEX8 r2 (EZ-USB FX CY7C646) | `0x1001`     |
| `0x1100`   | MIDEX3 (EZ-USB FX CY7C646) | `0x1101`        |

Upload uses the in-tree EZ-USB helper (`drivers/usb/misc/ezusb.c`,
`CONFIG_USB_EZUSB_FX2`). If your running kernel does not ship that helper,
the module still builds and works for already-operational devices, but
cannot upload firmware — `build.sh` warns about this. Load `ezusb` (e.g.
`sudo modprobe ezusb`) and replug the device in that case.

The firmware images live in [src/kernel/firmware/](src/kernel/firmware/) and
`./build.sh -i` installs them to `/lib/firmware/midex/`; the driver requests
them by name (`midex/midex8.fw`, `midex/midex8r2.fw`, `midex/midex3.fw`).

The MIDEX3 firmware (PID `0x1100`) is derived from the macOS loader kext and
has **not** been verified on real hardware, so it is gated behind a module
parameter and refuses to upload by default. To try it, load the module with
`allow_midex3_firmware=1`.

For the full wire-level protocol, firmware sources, and verification notes,
see [doc/firmware_upload_process.md](doc/firmware_upload_process.md).

In the 'doc' directory you will find some [analysis of the protocol](doc/analysis.md) in text and in wireshark files.

If you have a MIDEX3, I would love to hear from you: the firmware upload and
MIDI paths are implemented but the MIDEX3 firmware blob has not been confirmed
on real hardware. USB raw packet captures / pcap files would be very welcome.

## Issues

#### Normal machine

So far I haven't found any issues on my real machine (with Ubuntu 16.04 and a 4.4.0 kernel or with Ubuntu 20.04 and a 5.8.0 kernel).

#### Virtualbox

The following issues seem to occur only when using VirtualBox (with Ubuntu 16.04, a 4.8.0 kernel and guest img installed) for testing:
* empty packet is seen on EP2(out) or EP4(out)... This causes the EP (or device?) to hang/block/seize
* There seems to be some urbs never completing (mostly timing and led command/reply) some time after this empty packet is sent. Currently this is resolved by unlinking the urb and trying again the next time.
* in firmware 0x1001 mode, the re-submission of a midi-in urb (EP2(in)) sometimes takes longer than 1ms

