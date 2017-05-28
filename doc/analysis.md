# Steinberg MIDEX 8

## Device information:

USB midi device, 8 inputs, 8 outputs.

Vendor ID: 0x0a4e
Product ID: 0x1010, or with newer firmware 0x1001

Performing a hard reset changes the USB PID from 1001 to 1010.
Windows driver seems to update the firmware when it detects a MIDEX8 with PID 1010

The differences between the two firmware versions (that I can detect)
* Firmware 0x1010 - EP2(in) (midi-in) urb requests complete within 1 ms and can have empty response
* Firmware 0x1001 - EP2(in) (midi-in) urb requests block until data is available

## Endpoints:
EP 2: Midi timing/enabling? and midi in
* 0x02 - 2 - out - host sends [0f fd 00 00] (start), [0f f5 00 00] (stop) or [0f f9 00 00] . time interval is 25.6ms
* 0x82 - 2 - in - Midi in messages. URB in request only sent -after- the (start) condition on EP2out

EP 4: Midi out
* 0x04 - 4 - out - Midi out messages

EP 6: unknown, and LED control
* 0x06 - 6 - out - host sends: [7f 9a]  time interval is 50ms in idle mode, 190 ms or so when midi/timing is running
               - host sends LED commands at startup
* 0x86 - 6 - in  - midex replies [7f 9a] with [2f]. 


## Midi OUT Packet format
is 4 bytes, where 8 seems to contain 2x a 4byte message. It looks like the usb midi class...

WORD abbrev (capital):
	P = midex port
	S = status
	C = channel
	D = data
	X = unknown data

<message>
	<message> is either the channel message or a sysex message:
- channel message:
	[PS SC DD DD]

- sysex message:
	- Sysex start:
		[P4 f0 XX XX]
	- Sysex data:
		[P4 XX XX XX]
	- Sysex end:
		[P5 f7 00 00]
		[P6 XX f7 00]
		[P7 XX XX f7]

## Midi IN Packet format
is 8 bytes, where 16 seems to contain 2x 8byte message. It also looks like the usb midi class, except that there is this timing thingy before a message.

WORD abbrev:
	P = midex port
	S = status
	C = channel
	D = data
	X = unknown data

[P3f4XXXX] <message> ;
	XXXX seems to be an increasing counter (timer?) up to 4000
	<message> is either the channel message or a sysex message:
- channel message in the format of:
	[PS SC DD DD]
- sysex message
	- Sysex start:
		[P4 f0 XX XX]
	- Sysex data:
		[P4 XX XX XX]
	- Sysex end:
		[P5 f7 ?? ??]	- can show data but seems to be from un-cleared previous buffer
		[P6 XX f7 ??]	- can show data but seems to be from un-cleared previous buffer
		[P7 XX XX f7]

## What happens when Windows starts (with new firmware):
Windows start:
- get dev descriptors
- EP 6 in: IR, empty
- EP 6 out: IR, host sends: [fe 01]
- EP 6 in: IR, midex sends: [01]
- EP 2 out: IR, host sends [0f fd 00 00]
- EP 2 out: IR, host sends [0f f9 00 00]
- EP 2 out: IR, host sends [0f f9 00 00]
  EP 2 out continues to send regular [0f f9 00 00] until led GFX stop.
  last EP 2 out before windows shuts down: IR, host sends [0f f5 00 00]
- EP 6 out: send LED graphics
- EP 6 : send [7f 9a] - receive [2f]


## LED graphics data

[40 LL XX SS]
where
LL is the LED number (n = 0-7), formatted as: (44 | (n << 3))
XX is the LED state: 0xff = on, 0xfc = off
SS is the side (left/right, or in/out), where side = 1 or 2, and 00 for 'off'


