# MIDEX Firmware Upload Process

This document describes how the Steinberg MIDEX8 (and MIDEX3) firmware is
uploaded to the device's EZ-USB microcontroller. The goal is to be able to
implement firmware upload from the Linux kernel driver
[src/kernel/sound/usb/midex/midex.c](../src/kernel/sound/usb/midex/midex.c) so
the driver can take a "no firmware" device (PID `0x1000`, `0x1010`, `0x1100`)
and transition it to the operational PID (`0x1001` / `0x1101`).

## Background: chips and PIDs

| Revision    | Chip                                            | CPU reset reg (CPUCS) | Loadable RAM    |
| ----------- | ----------------------------------------------- | --------------------- | --------------- |
| MIDEX8      | Cypress EZ-USB AN2131 (8051 + USB SIE)          | `0x7F92`              | `0x0000-0x1FFF` |
| MIDEX8 r2   | Cypress EZ-USB FX CY7C646xx (enhanced 8051)     | `0x7F92`              | `0x0000-0x1B3F` |

Both chips share the same firmware-load protocol; the CPUCS register is at the
**same address (`0x7F92`)** on both. The current `fw.json` only writes up to
`0x1776`, which fits in either chip's RAM, so a single firmware image works for
both MIDEX8 revisions.

References:
- `doc/manuals/infineon-an2131-trm-usermanual-en.txt` §5.4 "Firmware Load"
- `doc/manuals/infineon-CY7C646-technical-reference-manual.txt` §5.4 "FirmwareLoad"

PIDs handled by the Steinberg Windows driver
[original_drivers/windows_driver_wine/midex8.inf](../original_drivers/windows_driver_wine/midex8.inf):

| PID      | Driver         | Meaning                                                 |
| -------- | -------------- | ------------------------------------------------------- |
| `0x1000` | `mdx8ldr.sys`  | MIDEX8 (AN2131) with no firmware → needs upload         |
| `0x1010` | `mdx8ldr.sys`  | MIDEX8 r2 (CY7C646 FX) with no firmware → needs upload  |
| `0x1001` | `midex8.sys`   | MIDEX8/r2 running operational firmware                  |
| `0x1100` | (mdx3ldr)      | MIDEX3 with no firmware → needs upload                  |
| `0x1101` | (operational)  | MIDEX3 running operational firmware                     |

The macOS loader kext explicitly distinguishes all three pre-firmware PIDs as
`Midex8FwLoader` (`0x1000`), `Midex8r2FwLoader` (`0x1010`) and
`Midex3FwLoader` (`0x1100`); each gets a *different* firmware blob (see the
macOS section below). The Windows INF only binds `0x1000` and `0x1010` to
`mdx8ldr.sys` — MIDEX3 was never shipped with the Windows driver in this
repo's collection.

The current kernel module already defines these in
[midex.c:66-71](../src/kernel/sound/usb/midex/midex.c#L66-L71) but binds only
to the operational PIDs.

## The "Anchor Download" protocol (vendor request `0xA0`)

Both EZ-USB families respond, while the 8051 is held in reset, to a built-in
vendor-specific USB control request that lets the host write into (or read
back from) the on-chip RAM. Cypress calls this the "Firmware Load" request,
and most documentation calls it the "Anchor Download" command.

Control transfer fields (host → device):

| Field            | Value                                                |
| ---------------- | ---------------------------------------------------- |
| `bmRequestType`  | `0x40` (vendor, host→device, recipient = device)     |
| `bRequest`       | `0xA0` ("Firmware Load")                             |
| `wValue`         | Target address in 8051 code/data RAM                 |
| `wIndex`         | `0` (reserved, must be 0)                            |
| `wLength`        | Number of payload bytes (≤ 64, typically 16)         |
| Data stage       | The bytes to write at `wValue`                       |

The reciprocal read-back uses the same `bRequest` with
`bmRequestType = 0xC0` (device→host).

Special address `0x7F92` is the **CPUCS** register; its LSB is the 8051 reset
line:

- Write `0x01` → assert reset (CPU halted, host can load RAM safely)
- Write `0x00` → release reset (CPU starts running from the new code)

The standard sequence the Cypress documentation describes is:

1. `wValue = 0x7F92, data = 0x01`  → hold 8051 in reset
2. Many `0xA0` writes loading code/data into RAM
3. `wValue = 0x7F92, data = 0x00`  → release 8051

## What the original Windows driver does, and what we actually need

The captured `fw.json` from the Windows driver looks like a *two-stage* load
(see breakdown below), but **we verified by experiment that the second
stage is not actually needed** — see "Empirical test: single-stage upload"
further down. The simple kernel-side implementation is a single flat loop
of `0xA0` writes while the 8051 stays in reset. The two-stage breakdown is
kept here as historical context and as a reference for what the original
artefacts contain.

## The MIDEX upload as captured: a **two-stage** download

A simple dump of `midex-pid-changer/fw.json` shows it is **not** a single
straight load — the CPUCS register is toggled twice in the middle of the
sequence:

```
packet #0   wValue=0x7F92 data=01    halt
packet #1   wValue=0x7F92 data=01    halt (redundant; same as Cypress example)
packets #2..#139   load stage-1 image (138 control transfers, 1664 bytes)
packet #140 wValue=0x7F92 data=00    RELEASE → stage-1 runs on the 8051
packet #141 wValue=0x7F92 data=01    halt again
packets #142..#472 load stage-2 image (331 control transfers, 5160 bytes)
packet #473 wValue=0x7F92 data=01    halt (redundant)
packet #474 wValue=0x7F92 data=00    RELEASE → stage-2 (real firmware) runs
```

Total: **475 control transfers**, **469 firmware-data writes**, **6824
firmware bytes** delivered.

### Stage 1: the "boot loader" (~1.6 KB)

Stage 1 only writes to a few address ranges:

| Address     | Size  | Contents                                    |
| ----------- | ----- | ------------------------------------------- |
| `0x0000`    | 3 B   | `02 17 53`  — `LJMP 0x1753` (8051 reset vec)|
| `0x0043`    | 3 B   | `02 15 00`  — `LJMP 0x1500` (USB ISR vec)   |
| `0x004B`    | 3 B   | `02 13 7F`  — `LJMP 0x137F` (I²C ISR vec)   |
| `0x1100`–`0x1776` | 1655 B | Stage-1 8051 code (entry point at `0x1753`) |

When CPUCS is released after stage 1, the CPU jumps from `0x0000` to
`0x1753`, which initialises the stack and falls through to `LJMP 0x146C`
(the body of the stage-1 program). Stage 1 stays resident in upper RAM
and provides a custom USB control handler that probably accepts and
applies stage 2; the device does **not** re-enumerate between stages — the
same `0xA0` mechanism is used for stage 2, but interpreted by the stage-1
code instead of the on-chip ROM loader.

Why two stages?  Two plausible reasons (we have not verified which):

1. **Address-range gating** — on some EZ-USB variants the built-in ROM
   loader only allows writes to part of the RAM (the lower 4 KB on EZ-USB
   FX in certain modes). Stage 1 lifts that restriction so stage 2 can
   write the full address range.
2. **Hardware initialisation / PID renumber prep** — stage 1 sets up I²C
   or GPIO state that has to be live before the real firmware runs.

For our purposes the *why* is academic: we just need to replay the
sequence.

### Stage 2: the operational firmware (~5.1 KB)

Stage 2 overwrites the interrupt vector table and the main code region:

| Address                | Size   | Contents                              |
| ---------------------- | ------ | ------------------------------------- |
| `0x0000`, `0x000B`, `0x001B`, `0x0033`, `0x0043`, `0x005B`, `0x0063` | 3 B each | All standard 8051 interrupt vectors |
| `0x0100`–`0x1512`      | 5139 B | Main operational firmware             |

The new reset vector is `LJMP 0x01E3`, so when CPUCS is released the
operational firmware begins at `0x01E3`. The device then renumerates and
reappears as PID `0x1001` (or `0x1101` for MIDEX3).

## Empirical test: single-stage upload also works

We verified on real hardware (PID `0x1000` → `0x1001`, MIDEX8 r1) that the
mid-sequence stage-1 execution is **not required**.
[midex-pid-changer/test_single_stage.py](../midex-pid-changer/test_single_stage.py)
strips the two CPUCS toggles at packet indices 140/141 and 473 from the
captured sequence, leaving:

```
1× CPUCS=1   (hold 8051 in reset)
469× 0xA0 data writes  (in original fw.json order, both stage-1 and stage-2 bytes)
1× CPUCS=0   (release 8051 → boot firmware)
```

Result over two consecutive runs: the device reliably renumerates to PID
`0x1001`. The script's final control transfer (`CPUCS=0`) consistently
returns `ENODEV (errno 19)`, which is the *expected* race between
"device processed the CPU release and renumerated" and "libusb completes
the control transfer round-trip" — not an error. The kernel-side
implementation must treat `-ENODEV` on the final CPUCS release as success.

Implication for re-enumeration timing (originally an open question): the
device renumerates **exactly once**, at the very end, as a side effect of
the 8051 being released. No mid-sequence re-enumeration happens. The
operational firmware presumably sets the EZ-USB `RENUM` bit during its
startup at `0x01E3` and forces a USB re-attach.

The original Windows driver's mid-sequence CPUCS toggles are therefore
either (a) a paranoid mimicry of Cypress' canonical anchor-download
example, or (b) needed for some MIDEX hardware revision we have not
tested. For MIDEX8 r1 the simpler form works; we should be conservative
about MIDEX3 / MIDEX8 r2 until tested, but the single-stage path is the
right *default*.

## Where the firmware bytes come from

Three independent sources contain the same firmware bytes:

1. **`midex-pid-changer/fw.json`** — captured replay of the Windows
   driver's control transfers. 475 packets. This is the source of truth
   used in this analysis.

2. **`original_drivers/windows_driver_wine/Mdx8ldr.sys`** — the Steinberg
   Windows loader driver. It embeds the firmware as a record table
   starting at file offset **`0x2667`** for stage 1 (138 records, ends at
   `0x3243`) and a second region for stage 2 (starts around `0x9DF`, with
   the main `0x0100..` block at `0xA79`). Each record is a fixed
   **22-byte slot**:

   ```
   offset 0..1  : wLength  (big-endian uint16, valid data byte count, ≤ 16)
   offset 2..3  : addrLow  (big-endian uint16)
   offset 4..5  : addrHigh (big-endian uint16)
   offset 6..21 : 16-byte data field (only the first wLength bytes are valid;
                                     the rest is zero padding)
   ```

   The target USB-control `wValue` is `addrHigh + addrLow`. (For all
   observed records `addrLow < 0x100`, so the split is really
   "page" + "offset within page".)

   This format is useful if we later want to extract firmware directly
   from the original driver instead of shipping a separate file.

3. **`original_drivers/mac/midexusbfwloaderdriver.pkg`** — the macOS
   firmware-loader kext. Analysed (see "MIDEX3 and MIDEX8 r2 firmware
   sources" below). Embeds **four** distinct record blobs:
   `_loader_firmware` (138 records, stage-1 boot loader), plus per-device
   stage-2 images `_midex8_firmware`, `_midex8r2_firmware`, and
   `_midex3_firmware`. The `_midex8r2_firmware` blob is bit-identical to
   the corresponding region of the Windows `mdx8ldr.sys`. The Mac
   `_midex8_firmware` is a slightly *different version* than the Windows
   one (LJMP destinations in the IRQ vector table are shifted by 5 bytes;
   the operational code is otherwise the same shape). The
   `_midex3_firmware` blob is **only** present in the Mac driver.

The `mdx8ldr.sys` VERSIONINFO identifies the firmware as
`"Midex 8 DW27c/m8r2pvf4 Firmware Download"` (version 1.4.0.0, 2002-07-12),
which can serve as an internal firmware-version tag.

## Reconstructed firmware artefacts in this repo

For convenience, the firmware blobs reconstructed from `fw.json` are
checked in under [doc/firmware/](firmware/):

| File                              | Description                                              |
| --------------------------------- | -------------------------------------------------------- |
| `midex_firmware_stage1.ihx`       | Stage-1 boot loader (MIDEX8 r1, Windows-derived), Intel HEX |
| `midex_firmware_stage2.ihx`       | Stage-2 operational firmware (MIDEX8 r1, Windows-derived), Intel HEX |
| `midex_firmware_combined.ihx`     | Final post-stage-2 RAM contents (MIDEX8 r1), Intel HEX (reference) |
| `midex8_firmware_combined.bin`    | MIDEX8 r1 combined image as raw binary (6007 B, addr 0..0x1776) |
| `midex8r2_combined.ihx` / `.bin`  | MIDEX8 **r2** (CY7C646 FX) stage-2 firmware, Mac/Win bit-identical |
| `midex8r2_records.bin`            | MIDEX8 r2 stage-2 raw record table (440 records × 22 B)  |
| `midex3_combined.ihx` / `.bin`    | MIDEX3 stage-2 firmware (Mac-only source)                |
| `midex3_records.bin`              | MIDEX3 stage-2 raw record table (359 records × 22 B)     |
| `midex_mac_loader.ihx`            | Stage-1 boot loader as embedded in the Mac kext (shared across all three devices) |
| `midex_mac_loader_records.bin`    | Stage-1 raw record table (138 records × 22 B)            |

The Intel HEX format is the conventional way to ship EZ-USB firmware (the
Cypress development tools and many Linux fxload-style loaders consume it).
A future kernel implementation can either:

- Ship the two `.ihx` files as separate firmware blobs via
  `request_firmware()` (one per stage), or
- Embed a small struct array of `(addr, len, bytes)` records statically in
  the module, or
- Ship a binary identical to `fw.json` (raw control-transfer log) for
  byte-perfect replay.

## MIDEX3 and MIDEX8 r2 firmware sources

Following [midex3_firmware_plan.md](midex3_firmware_plan.md), the macOS
firmware-loader kext was unpacked and the embedded firmware blobs were
extracted. The relevant binary is the universal Mach-O kext at:

```
original_drivers/mac/midexusbfwloaderdriver.pkg/Payload
  → MidexUsbFwLoaderDriver.kext/Contents/MacOS/MidexUsbFwLoaderDriver
```

It declares three `IOKitPersonalities` — `Midex8FwLoader` (`PID 0x1000`),
`Midex8r2FwLoader` (`PID 0x1010`), `Midex3FwLoader` (`PID 0x1100`) — and
exports four data symbols (visible via `llvm-nm`):

| Symbol               | Size (B) | Records | Purpose                                  |
| -------------------- | -------- | ------- | ---------------------------------------- |
| `_loader_firmware`   | 3072     | 138     | Stage-1 boot loader (used for all three devices) |
| `_midex8_firmware`   | 7328     | 331     | Stage-2 for MIDEX8 r1 (AN2131)           |
| `_midex8r2_firmware` | 9948     | 440     | Stage-2 for MIDEX8 r2 (CY7C646 FX)       |
| `_midex3_firmware`   | 7936     | 359     | Stage-2 for MIDEX3 (CY7C646 FX)          |

Each blob uses the same 22-byte record format already documented for
`Mdx8ldr.sys`: 2 B little-endian length, 2 B little-endian address, 1 B
page byte (always `0x00`), 17 B data payload (only the first `length`
bytes valid). End-of-table is a zero-length record. `loader_firmware` and
all three `midexN_firmware` blobs decode cleanly under this format, and
the 138/331 record counts exactly match the stage-1 / stage-2 packet
counts in `fw.json`.

### Cross-source check

- The Mac `_midex8r2_firmware` is **bit-identical** to a 9680-byte region
  inside `original_drivers/windows_driver_wine/Mdx8ldr.sys` (starting at
  file offset `0x3260`). Two independent sources, same bytes — high
  confidence the r2 firmware is genuine.
- The Mac `_midex8_firmware` is **not** bit-identical to its Windows
  counterpart. The first record (LJMP at the reset vector to `0x01E3`) is
  identical, but subsequent IRQ-vector LJMPs land 5 bytes earlier (e.g.
  Timer 0 vector → `LJMP 0x0A55` on Mac vs `LJMP 0x0A5A` in Windows;
  Timer 1 → `0x0AA5` vs `0x0AAA`). The operational code blocks have the
  same shape but the offsets indicate the Mac build is a slightly
  different revision. For MIDEX8 r1 the **Windows-derived
  `midex8_firmware_combined.bin` remains the source of truth** since it
  is the version verified to upload successfully on real hardware.
- The Mac `_midex3_firmware` does **not** appear anywhere in the Windows
  loader binaries shipped in this repo (neither
  `original_drivers/windows_driver_wine/Mdx8ldr.sys` nor
  `original_drivers/Midex8_Vista_Win7_x64_driver/mdx8ldr.sys`, nor the
  2002 installers `Midex8_V1_80.exe` / `updmros.exe`). The Mac kext is
  the **only available source** for MIDEX3 firmware in this repo.

### Memory-map differences confirm separate chip targets

All MIDEX8 r1 records fit in `0x0000-0x1508` (well inside the AN2131
8 KB RAM), as do the MIDEX3 records (max `0x1692`). The MIDEX8 r2
firmware additionally contains a single 1-byte write to `0x7FE5` — that
address only exists on the CY7C646 FX family (FX configuration register
region). Uploading `midex8r2_firmware` to an AN2131 chip would silently
write to an invalid address; conversely, an AN2131-targeted blob will
miss the FX register initialisation needed on r2. The blobs are
genuinely device-specific and cannot be substituted for each other.

### Outcome

This is **Case B** of [midex3_firmware_plan.md](midex3_firmware_plan.md)
("separate but compatible images"). A kernel implementation that uploads
firmware for `0x1000`, `0x1010`, or `0x1100` must:

1. Always upload the shared stage-1 (`loader_firmware`) first.
2. Pick the device-specific stage-2 by PID:
   - `0x1000` → `midex8_firmware_combined.bin` (Windows-derived, verified)
   - `0x1010` → `midex8r2_combined.bin` (Mac/Win bit-identical)
   - `0x1100` → `midex3_combined.bin` (Mac-only, **not yet verified on
     real hardware**)

For MIDEX3 we have a firmware blob but no captured upload trace and no
physical device. The MIDEX3 upload path should therefore default to
"untested — refuse unless an override is set" until someone with the
hardware confirms a clean `0x1100 → 0x1101` transition; see the open
question at the bottom of this file.

## Implementation plan for the kernel module

A minimum viable implementation:

1. Extend the `usb_device_id` table in
   [src/kernel/sound/usb/midex/midex.c](../src/kernel/sound/usb/midex/midex.c)
   to also match `0x1000`, `0x1010`, `0x1100` — and in `.probe`, check the
   PID:

   - If PID ∈ `{0x1000, 0x1010, 0x1100}` → run firmware upload, then return
     `-ENODEV` (or rely on the upcoming re-enumeration) so the next
     enumeration with the operational PID binds normally.
   - If PID ∈ `{0x1001, 0x1101}` → proceed with the existing initialisation.

2. The upload routine itself does, using `usb_control_msg()`:

   ```c
   #define MIDEX_CPUCS_ADDR  0x7F92
   #define MIDEX_FW_REQ      0xA0
   #define MIDEX_FW_REQTYPE  (USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE) /* 0x40 */

   /* Write one byte to CPUCS */
   static int sb_midex_cpu_reset(struct usb_device *udev, u8 reset)
   {
       return usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
                              MIDEX_FW_REQ, MIDEX_FW_REQTYPE,
                              MIDEX_CPUCS_ADDR, 0, &reset, 1, 5000);
   }

   /* Write one block to on-chip RAM */
   static int sb_midex_fw_write(struct usb_device *udev,
                                u16 addr, const u8 *data, u16 len)
   {
       return usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
                              MIDEX_FW_REQ, MIDEX_FW_REQTYPE,
                              addr, 0, (u8 *)data, len, 5000);
   }
   ```

   The full sequence is the **single-stage** form (verified to work — see
   "Empirical test" above):

   ```
   sb_midex_cpu_reset(udev, 1);                  /* halt 8051 */
   for each record in combined firmware:
       sb_midex_fw_write(udev, rec.addr, rec.data, rec.len);
   ret = sb_midex_cpu_reset(udev, 0);            /* release 8051 */
   if (ret == -ENODEV)
       ret = 0;                                  /* expected: device renumerated */
   ```

   The python replay uses a 10 ms delay after each transfer; in the
   kernel we can drop the delay (control transfers are already
   serialised) but keep the ordering of writes from `fw.json` so the new
   reset vector lands last.

3. After the CPUCS release the device re-enumerates with the operational
   PID (`0x1001`) and the driver's existing probe path runs.

## Open questions to verify before implementing

- [x] ~~Does Stage 1 actually need to *run* between the two halt/release
      pairs?~~ **No.** Verified by
      [test_single_stage.py](../midex-pid-changer/test_single_stage.py)
      on MIDEX8 r1: a single CPUCS=1 → all writes → CPUCS=0 sequence
      transitions the device cleanly from `0x1000` to `0x1001`. The
      Cypress ROM loader handles writes to the full 8 KB on AN2131
      without help from a secondary loader.
- [x] ~~Does the device re-enumerate between stages?~~ **No.** It
      renumerates exactly once, at the end, when the 8051 is released
      and the operational firmware sets `RENUM`. The kernel-side upload
      must accept `-ENODEV` on the final CPUCS=0 as success.
- [ ] On MIDEX3 the operational PID is `0x1101`; verify on real hardware
      whether uploading `loader_firmware` + `midex3_combined.bin` (both
      extracted from the macOS kext — see "MIDEX3 and MIDEX8 r2 firmware
      sources" above) transitions a `0x1100` device to `0x1101`, and
      whether the single-stage upload form works for MIDEX3 too. We have
      no MIDEX3 device in-house. Until verified, the kernel default for
      `0x1100` should be to log "untested — Mac-derived firmware, no
      hardware confirmation" and refuse to upload unless an explicit
      module parameter / sysfs knob is set.
- [x] ~~On MIDEX8 r2 (PID `0x1010`), verify on real hardware that uploading
      `loader_firmware` + `midex8r2_combined.bin` transitions to the
      operational PID.~~ **Verified.** Single-stage upload via
      [`test_single_stage.py`](../midex-pid-changer/test_single_stage.py)
      transitions a real r2 device from `0x1010` to its operational PID.

## Reverse-engineering pointers (next session)

If we want full clarity on stage 1's behaviour or the exact decision logic
the Windows driver uses, the next step is to **disassemble the two
relevant binaries** in Ghidra:

1. **`original_drivers/windows_driver_wine/Mdx8ldr.sys`** — 29 KB Windows
   kernel driver (PE32, i386). In Ghidra:
   - `File → Import` → select `Mdx8ldr.sys`. Choose the default PE32 loader.
   - Run auto-analysis with the standard settings (Windows ABI).
   - Look at the `INIT` and `.text` sections; the firmware embed is in
     `.data` at file offsets `0x2667` (stage 1) and ~`0x9DF`/`0xA79`
     (stage 2). Cross-reference those bytes to find the routine that
     reads the record table and issues `IOCTL_INTERNAL_USB_SUBMIT_URB`
     control transfers. That routine encodes the high-level sequence
     (including the CPUCS toggles between stages).
   - Useful symbols to look for in the strings list (already visible
     today): `mxd8ldr_Unload`, `entering ... DriverEntry`, the version
     string `"Midex 8 DW27c/m8r2pvf4 Firmware Download"`.

2. **`original_drivers/windows_driver_wine/Midex8.sys`** — 88 KB
   operational driver. Not strictly needed for upload, but contains the
   client view of EP2/EP4/EP6 transfers and is a useful cross-reference
   for our existing kernel code in
   [src/kernel/sound/usb/midex/midex.c](../src/kernel/sound/usb/midex/midex.c).

3. The **8051 firmware itself** can be loaded into Ghidra as well:
   - `File → Import → Format: Binary, Language: 8051:LE:16:default`.
   - Base address `0x0000`, load `doc/firmware/midex8_firmware_combined.bin`.
   - Entry point: `0x01E3` (set the function start there).
   - This is the most direct path to understanding the device's MIDI
     scheduling and timing logic, including the EP2 timing protocol and
     LED commands described in [doc/analysis.md](analysis.md).

## Status

- [x] Decoded the wire-level upload protocol from `fw.json`.
- [x] Identified the two-stage structure in the captured upload.
- [x] Extracted clean firmware artefacts under `doc/firmware/`.
- [x] Confirmed CPUCS at `0x7F92` works for both AN2131 and CY7C646.
- [x] **Verified on real hardware that a single-stage upload works on
      MIDEX8 r1 (`0x1000` → `0x1001`)**, so the kernel can use a simple
      flat loop.
- [x] **Confirmed re-enumeration timing: exactly one renumeration, at
      the end, triggered by the final CPUCS=0.** The kernel must accept
      `-ENODEV` on that transfer as success.
- [x] **Investigated the macOS / older Windows drivers** (per
      [midex3_firmware_plan.md](midex3_firmware_plan.md)): the macOS
      loader kext embeds three distinct stage-2 firmware blobs
      (`_midex8_firmware`, `_midex8r2_firmware`, `_midex3_firmware`) plus
      a shared stage-1 (`_loader_firmware`). MIDEX8 r2 firmware is
      bit-identical between Mac and the Windows `Mdx8ldr.sys`; MIDEX3
      firmware is Mac-only. See "MIDEX3 and MIDEX8 r2 firmware sources"
      above. Extracted artefacts checked in under
      [doc/firmware/](firmware/).
- [ ] Optionally decompile `Mdx8ldr.sys` in Ghidra to learn why the
      original Windows driver bothers with the mid-sequence stage-1
      execution (curiosity only — not needed for the kernel implementation).
- [x] **Implement upload in
      [src/kernel/sound/usb/midex/midex.c](../src/kernel/sound/usb/midex/midex.c)**.
      The driver now binds to the loader PIDs and delegates to the in-tree
      EZ-USB helper (`ezusb_fx1_ihex_firmware_download`) using the
      pre-built blobs in [src/kernel/firmware/](../src/kernel/firmware/).
      MIDEX3 (0x1100) is gated behind `allow_midex3_firmware=1` until
      hardware confirms the Mac-derived blob works.
