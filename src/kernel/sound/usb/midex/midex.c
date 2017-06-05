/*
 * Steinberg Midex 8 driver
 *
 *   Copyright (C) 2017 Hedde Bosman (sgorpi@gmail.com)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/bitmap.h>

#include <linux/mutex.h>
#include <linux/spinlock.h>

#include <linux/usb.h>
#include <linux/usb/audio.h>

#include <linux/ktime.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/rawmidi.h>
#include <sound/asound.h>

/*******************************************************************
 * Defines
 *******************************************************************/
#define SB_MIDEX_PREFIX "snd-usb-midex: "

#define SB_MIDEX_URB_BUFFER_SIZE 	64
#define SB_MIDEX_NUM_URBS_PER_EP	7

/* Timer periods (in ms) */
//#define TIMER_PERIOD_TIMING_NS	(25598*1000)
#define TIMER_PERIOD_TIMING_NS	(25600*1000)

#define TIMER_PERIOD_LED_WHEN_INACTIVE_MS	 50
#define TIMER_PERIOD_LED_WHEN_ACTIVE_MS		 150


/* a substream == a midi port */
/*
 * VID is always 0x0a4e 
 * For Midex 8, PID can be 0x1001, or 0x1010 (after some device reset)
 */

/* Theoretically, we might also support the midex3, but I don't have one. Add it here  */
/* PID1 seems to imply: reflash firmware */
#define SB_MIDEX8_PID1 0x1010
#define SB_MIDEX8_PID2 0x1001

/*******************************************************************
 * Type definitions
 *******************************************************************/

static struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x0a4e, SB_MIDEX8_PID1) },
	{ USB_DEVICE(0x0a4e, SB_MIDEX8_PID2) },
	{ },
};


enum sb_midex_type {
	SB_MIDEX_TYPE_UNKNOWN,
	SB_MIDEX_TYPE_3,  /* 1 in, 3 out*/
	SB_MIDEX_TYPE_8, /* 8 in, 8 out*/
}; /* card type */

enum sb_midex_port_state {
	STATE_UNKNOWN	= 0,
	STATE_1PARAM	= 1,
	STATE_2PARAM_1	= 2,
	STATE_2PARAM_2	= 3,
	STATE_SYSEX_0	= 4,
	STATE_SYSEX_1	= 5,
	STATE_SYSEX_2	= 6,
};

enum sb_midex_timing_state {
	SB_MIDEX_TIMING_IDLE = 0,
	SB_MIDEX_TIMING_START,
	SB_MIDEX_TIMING_RUNNING,
	SB_MIDEX_TIMING_STOP,
};

enum sb_midex_led_state {
	SB_MIDEX_LED_RUNNING = 0,
	SB_MIDEX_LED_INIT,
	SB_MIDEX_LED_GFX_RUN_OUT,
	SB_MIDEX_LED_GFX_FILL_IN,
	SB_MIDEX_LED_GFX_RUN_IN,
};


struct sb_midex;

struct sb_midex_port {
	struct snd_rawmidi_substream *substream;
	int triggered;
	enum sb_midex_port_state state;
	uint8_t midi_data[2];
};

struct sb_midex_urb_ctx {
	struct urb *urb;
	struct sb_midex *midex;
	bool active;
};

struct sb_midex_endpoint {
	struct sb_midex_port ports[8];
	struct sb_midex_urb_ctx urbs[SB_MIDEX_NUM_URBS_PER_EP];
	int num_ports;
	int last_active_port;

	spinlock_t lock;
	bool active;
};

struct sb_midex {
	struct usb_device *usbdev;
	struct snd_card *card;
	struct usb_interface *intf;
	int card_index;
	int card_type;


	struct snd_rawmidi *rmidi;
	int num_used_substreams;

	/* Timer */
	spinlock_t        timer_timing_lock;
	enum sb_midex_timing_state timing_state;
	struct hrtimer    timer_timing;
	struct timer_list timer_led;
	ktime_t 		  timer_timing_deltat;

	/* LED */
	enum sb_midex_led_state led_state;
	int led_state_gfx;
	int led_num_packets_to_send;

	/* MIDI */
	struct tasklet_struct midi_out_tasklet;
	struct sb_midex_endpoint midi_in;	/* EP 2 in */
	struct sb_midex_endpoint midi_out;	/* EP 4 out */

	/* other */
	struct sb_midex_urb_ctx timing_out_urb[SB_MIDEX_NUM_URBS_PER_EP]; 	/* EP 2 out */
	struct sb_midex_urb_ctx led_commands_urb[SB_MIDEX_NUM_URBS_PER_EP];	/* EP 6 out */
	struct sb_midex_urb_ctx led_replies_urb;	/* EP 6 in */


	struct usb_anchor anchor;
};


/*******************************************************************
 * Function declarations
 *******************************************************************/

static void sb_midex_usb_midi_input_start(struct sb_midex *midex);
static void sb_midex_usb_midi_input_stop(struct sb_midex *midex);



/*******************************************************************
 * Internal global variables
 *******************************************************************/

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;

static DEFINE_MUTEX(devices_mutex);
static DECLARE_BITMAP(devices_used, SNDRV_CARDS);
static struct usb_driver sb_midex_driver;

static const uint8_t sb_midex_cin_length[] = {
	0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1
//	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b, c, d, e, f
};


/*
 * Submits the URB, with error handling.
 */
static int sb_midex_submit_urb(struct sb_midex_urb_ctx* ctx, gfp_t flags, const char* function) {
	int err = 0;

	err = usb_submit_urb(ctx->urb, flags);

	if (err < 0)
		dev_err(&ctx->urb->dev->dev, SB_MIDEX_PREFIX "usb_submit_urb: %d at %s\n", err, function);
	else
		ctx->active = true;

	return err;
}


static int sb_midex_urb_show_error(const struct urb *urb, const char *func) {
	switch (urb->status) {
		/* manually unlinked, or device gone */
		case -ENOENT:
		case -ECONNRESET:
		case -ESHUTDOWN:
		case -ENODEV:
			return -ENODEV;
		/* errors that might occur during unplugging */
		case -EPROTO:
		case -ETIME:
		case -EILSEQ:
			return -EIO;
		default:
			dev_err(&urb->dev->dev, "urb status %d in %s\n", urb->status, func);
			return 0; /* continue */
	}
}


static void sb_midex_urb_and_buffer_free(struct sb_midex *midex, struct urb *urb, unsigned int buffer_length) {
	if (urb)
		usb_free_coherent(midex->usbdev, buffer_length, urb->transfer_buffer, urb->transfer_dma);

	usb_free_urb(urb);
}


static struct urb * sb_midex_urb_and_buffer_alloc(
		struct sb_midex *midex,
		unsigned int pipe,
		unsigned int buffer_length,
		usb_complete_t complete_fn,
		void *context)
{
	static struct urb *urb = NULL;
	void* buffer = NULL;

	urb = usb_alloc_urb(0, GFP_KERNEL);

	if (urb != NULL) {
		buffer = usb_alloc_coherent(
				midex->usbdev,
				buffer_length, GFP_KERNEL,
				&urb->transfer_dma);

		if (buffer != NULL) {
			usb_fill_int_urb(
					urb, midex->usbdev,
					pipe,
					buffer, buffer_length,
					complete_fn, context, 1);

			urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		} else {
			dev_warn(&midex->usbdev->dev, SB_MIDEX_PREFIX "could not allocate urb buffer of %db\n", buffer_length);
		}
	} else {
		dev_warn(&midex->usbdev->dev, SB_MIDEX_PREFIX "could not allocate urb\n");
	}
	return urb;
}


/*********************************************************************************
 * MIDI stream open/close functions
 *********************************************************************************/
static int sb_midex_raw_midi_substream_open(struct snd_rawmidi_substream *substream) {
	struct sb_midex *midex = substream->rmidi->private_data;

	midex->num_used_substreams++;
	if (midex->num_used_substreams > 0 && midex->timing_state == SB_MIDEX_TIMING_IDLE)
		midex->timing_state = SB_MIDEX_TIMING_START;

	return 0;
}


static int sb_midex_raw_midi_substream_close(struct snd_rawmidi_substream *substream) {
	struct sb_midex *midex = substream->rmidi->private_data;

	midex->num_used_substreams--;
	if (midex->num_used_substreams <= 0 && midex->timing_state != SB_MIDEX_TIMING_IDLE)
		midex->timing_state = SB_MIDEX_TIMING_STOP;

	return 0;
}


/*********************************************************************************
 * MIDI input functions
 *********************************************************************************/

static int sb_midex_raw_midi_input_open(struct snd_rawmidi_substream *substream) {
	return sb_midex_raw_midi_substream_open(substream);
}


static int sb_midex_raw_midi_input_close(struct snd_rawmidi_substream *substream) {
	return sb_midex_raw_midi_substream_close(substream);
}


static void sb_midex_raw_midi_input_trigger(struct snd_rawmidi_substream *substream, int up) {
	struct sb_midex *midex = substream->rmidi->private_data;

	midex->midi_in.last_active_port = substream->number;
	midex->midi_in.ports[substream->number].triggered = up;
}



/*********************************************************************************
 * MIDI output functions
 *********************************************************************************/

static int sb_midex_raw_midi_output_open(struct snd_rawmidi_substream *substream) {
	return sb_midex_raw_midi_substream_open(substream);
}


static int sb_midex_raw_midi_output_close(struct snd_rawmidi_substream *substream) {
	return sb_midex_raw_midi_substream_close(substream);
}


static void sb_midex_raw_midi_output_trigger( struct snd_rawmidi_substream *substream, int up) {
	struct sb_midex *midex = substream->rmidi->private_data;

	midex->midi_out.last_active_port = substream->number;
	midex->midi_out.ports[substream->number].triggered = up;

	if (up)
		tasklet_schedule(&midex->midi_out_tasklet);
}


static struct snd_rawmidi_ops sb_midex_raw_midi_output = {
	.open =    sb_midex_raw_midi_output_open,		/* when a substream (port) is opened */
	.close =   sb_midex_raw_midi_output_close,		/* when a substream (port) is closed */
	.trigger = sb_midex_raw_midi_output_trigger,	/* up!=0: when there is some data in a substream (port)
												 * up==0: when the transmission of data should be aborted */
};


static struct snd_rawmidi_ops sb_midex_raw_midi_input = {
	.open =    sb_midex_raw_midi_input_open,		/* when a substream (port) is opened */
	.close =   sb_midex_raw_midi_input_close,		/* when a substream (port) is closed */
	.trigger = sb_midex_raw_midi_input_trigger,		/* up!=0: enable receiving data
												 * up==0: disable receiving data */
};


/*********************************************************************************
 * USB functions
 *********************************************************************************/

static void sb_midex_usb_midi_input_start(struct sb_midex *midex) {
	int i;

	if (midex->midi_in.num_ports == 0)
		return;

	if (!midex->midi_in.active) {
		midex->midi_in.active = true;

		for (i = 0; i < SB_MIDEX_NUM_URBS_PER_EP; i++) {
			sb_midex_submit_urb(&midex->midi_in.urbs[i], GFP_ATOMIC, __func__);
		}
	}
}


static void sb_midex_usb_midi_input_stop(struct sb_midex *midex) {
	int i;

	if (midex->midi_in.num_ports == 0)
		return;

	for (i = 0; i < SB_MIDEX_NUM_URBS_PER_EP; ++i) {
		usb_unlink_urb(midex->midi_in.urbs[i].urb);
	}

	midex->midi_in.active = false;
}


static void sb_midex_usb_midi_input_to_raw_midi(
		struct sb_midex *midex,
		const unsigned char *buffer,
		unsigned int buf_len)
{
	unsigned long flags;
	int i;
	unsigned char port;
	unsigned char status;
	unsigned char out_len;

	/* We expect midi input in blocks of 4 bytes. Warn if we get weird sizes */
	if ((buf_len & 0x03) || (buf_len > SB_MIDEX_URB_BUFFER_SIZE)) {
		dev_warn(&midex->usbdev->dev, SB_MIDEX_PREFIX "unexpected midi input length: %d\n", buf_len);
	}

	spin_lock_irqsave(&midex->midi_in.lock, flags);

	for (i = 0; i < buf_len; i += 4) {
		port = (buffer[i] >> 4) & 0x07;


		status = buffer[i] & 0x0f;
		switch(status) {
			case 0x03: /* time info. We ignore it for now */
				out_len = 0;
				break;
			case 0x05: /* end of sysex, 1 byte */
				out_len = 1;
				break;
			case 0x06: /* end of sysex, 2 bytes */
				out_len = 2;
				break;
			case 0x07: /* end of sysex, 3 bytes */
			case 0x04: /* sysex, 3 bytes */
			default: /* anything else, 3 bytes*/
				out_len = 3;
				break;
				// TODO: see if other inputs would need different length from sb_midex_cin_length
		}

		if ((out_len > 0) &&
				midex->midi_in.ports[port].triggered &&
				midex->midi_in.ports[port].substream &&
				midex->midi_in.ports[port].substream->opened)
		{
			snd_rawmidi_receive(midex->midi_in.ports[port].substream, &buffer[i+1], out_len);
		}
	}
	spin_unlock_irqrestore(&midex->midi_in.lock, flags);
}


static void sb_midex_usb_midi_input_complete(struct urb *urb) {
	unsigned long flags;

	struct sb_midex_urb_ctx *ctx = urb->context;
	struct sb_midex *midex = ctx->midex;

	if (!midex || urb->status == -ESHUTDOWN)
		return;

	/* Process data and submit it again */
	if (urb->status) {
		sb_midex_urb_show_error(urb, __func__);
	} else {
		/* do some processing */
		sb_midex_usb_midi_input_to_raw_midi(midex, urb->transfer_buffer, urb->actual_length);
	}

	spin_lock_irqsave(&midex->timer_timing_lock, flags);

	if (midex->midi_in.active && midex->timing_state == SB_MIDEX_TIMING_RUNNING) {
		// for some reason, an empty urb sometimes ends up at the timing output, so we use timer_timing_lock
		sb_midex_submit_urb(ctx, GFP_ATOMIC, __func__);
	} else {
		ctx->active = false;
	}

	spin_unlock_irqrestore(&midex->timer_timing_lock, flags);
}


/*
 * Adds one USB MIDI packet to the output buffer.
 * \note (Same the ALSA USB MIDI driver's snd_usbmidi_output_standard_packet(...))
 */
static void sb_midex_usb_midi_output_packet(
		struct urb *urb,
		uint8_t p0,	uint8_t p1,
		uint8_t p2,	uint8_t p3)
{
	uint8_t *buf = (uint8_t *)urb->transfer_buffer + urb->transfer_buffer_length;
	buf[0] = p0;
	buf[1] = p1;
	buf[2] = p2;
	buf[3] = p3;
	urb->transfer_buffer_length += 4;
}


/**
 * Converts MIDI commands to USB MIDI packets.
 *
 * \note (Same the ALSA USB MIDI driver's snd_usbmidi_transmit_byte(...))
 */
static void sb_midex_usb_midi_output_transmit_byte(
		struct sb_midex_port *port,
		uint8_t b,
		struct urb *urb)
{
	uint8_t p0 = (port->substream->number & 0x7) << 4;

	if (b >= 0xf8) {
		sb_midex_usb_midi_output_packet(urb, p0 | 0x0f, b, 0, 0);
	} else if (b >= 0xf0) {
		switch (b) {
			case 0xf0:
				port->midi_data[0] = b;
				port->state = STATE_SYSEX_1;
				break;
			case 0xf1:
			case 0xf3:
				port->midi_data[0] = b;
				port->state = STATE_1PARAM;
				break;
			case 0xf2:
				port->midi_data[0] = b;
				port->state = STATE_2PARAM_1;
				break;
			case 0xf4:
			case 0xf5:
				port->state = STATE_UNKNOWN;
				break;
			case 0xf6:
				sb_midex_usb_midi_output_packet(urb, p0 | 0x05, 0xf6, 0, 0);
				port->state = STATE_UNKNOWN;
				break;
			case 0xf7:
				switch (port->state) {
				case STATE_SYSEX_0:
					sb_midex_usb_midi_output_packet(urb, p0 | 0x05, 0xf7, 0, 0);
					break;
				case STATE_SYSEX_1:
					sb_midex_usb_midi_output_packet(urb, p0 | 0x06, port->midi_data[0], 0xf7, 0);
					break;
				case STATE_SYSEX_2:
					sb_midex_usb_midi_output_packet(urb, p0 | 0x07, port->midi_data[0], port->midi_data[1], 0xf7);
					break;
				default:
					break;
				}
				port->state = STATE_UNKNOWN;
				break;
		}
	} else if (b >= 0x80) {
		port->midi_data[0] = b;
		if (b >= 0xc0 && b <= 0xdf)
			port->state = STATE_1PARAM;
		else
			port->state = STATE_2PARAM_1;
	} else { /* b < 0x80 */
		switch (port->state) {
			case STATE_1PARAM:
				if (port->midi_data[0] < 0xf0) {
					p0 |= port->midi_data[0] >> 4;
				} else {
					p0 |= 0x02;
					port->state = STATE_UNKNOWN;
				}
				sb_midex_usb_midi_output_packet(urb, p0, port->midi_data[0], b, 0);
				break;
			case STATE_2PARAM_1:
				port->midi_data[1] = b;
				port->state = STATE_2PARAM_2;
				break;
			case STATE_2PARAM_2:
				if (port->midi_data[0] < 0xf0) {
					p0 |= port->midi_data[0] >> 4;
					port->state = STATE_2PARAM_1;
				} else {
					p0 |= 0x03;
					port->state = STATE_UNKNOWN;
				}
				sb_midex_usb_midi_output_packet(urb, p0, port->midi_data[0], port->midi_data[1], b);
				break;
			case STATE_SYSEX_0:
				port->midi_data[0] = b;
				port->state = STATE_SYSEX_1;
				break;
			case STATE_SYSEX_1:
				port->midi_data[1] = b;
				port->state = STATE_SYSEX_2;
				break;
			case STATE_SYSEX_2:
				sb_midex_usb_midi_output_packet(urb, p0 | 0x04, port->midi_data[0], port->midi_data[1], b);
				port->state = STATE_SYSEX_0;
				break;
			default:
				break;
		}
	}
}


static int sb_midex_usb_midi_output_from_raw_midi(struct sb_midex *midex, struct urb *urb) {
	int i;
	int port_nr = 0;
	uint8_t b;
	struct sb_midex_port *midi_port;

	for (i = 0; i < midex->midi_out.num_ports; ++i) {
		port_nr = i;//(midex->midi_out.last_active_port + i) % midex->midi_out.num_ports;
		midi_port = &midex->midi_out.ports[port_nr];

		if (midi_port->substream == NULL)
			dev_info(&midex->usbdev->dev, SB_MIDEX_PREFIX "substream = NULL...\n");
		if (urb == NULL)
			dev_info(&midex->usbdev->dev, SB_MIDEX_PREFIX "urb = NULL...\n");

		// Have seen cases with up to 20 bytes sent in 1 packet. Assuming the full buffer can be used:
		while ((urb->transfer_buffer_length + 3 < SB_MIDEX_URB_BUFFER_SIZE) &&
				(midi_port->triggered > 0) && /* make sure it's triggered, otherwise it crashes */
				(midi_port->substream->opened) &&
				(snd_rawmidi_transmit(midi_port->substream, &b, 1) == 1))
		{
			sb_midex_usb_midi_output_transmit_byte(midi_port, b, urb);
		}
	}
	// give the next port some attention the next round:
	midex->midi_out.last_active_port ++;
	midex->midi_out.last_active_port %= midex->midi_out.num_ports;

	return 0;
}


static void sb_midex_usb_midi_output(struct sb_midex *midex) {
	unsigned long flags;
	int i;
	bool found_urb = false;

	spin_lock_irqsave(&midex->midi_out.lock, flags);
	// check if device is connected or sth, if not return

	/* find a free urb, and read data from raw midi, until either no free urb or no data. */
	for (i = 0; i < SB_MIDEX_NUM_URBS_PER_EP; ++i) {
		if (!midex->midi_out.urbs[i].active) {
			found_urb = true;
			midex->midi_out.urbs[i].urb->transfer_buffer_length = 0;
			/* get output from raw_midi */
			sb_midex_usb_midi_output_from_raw_midi(midex, midex->midi_out.urbs[i].urb);

			if (midex->midi_out.urbs[i].urb->transfer_buffer_length > 0) {
				sb_midex_submit_urb(&midex->midi_out.urbs[i], GFP_ATOMIC, __func__);
			} else { // no more data found
				break;
			}
		}
	}

	if (!found_urb) {
		dev_info(&midex->usbdev->dev, SB_MIDEX_PREFIX "midi out has no free urb yet.\n");
		/* unlink all urbs...  */
		/*for (i = 0; i < SB_MIDEX_NUM_URBS_PER_EP; ++i)
			usb_unlink_urb(midex->midi_out.urbs[i].urb); */
	}
	spin_unlock_irqrestore(&midex->midi_out.lock, flags);

	/* Assume the completion handler gets called, and thus any unsent data still gets sent in the next round... */
}


static void sb_midex_usb_midi_output_complete(struct urb *urb) {
	/* lock EP, find urb, set urb to inactive, unlock */
	struct sb_midex_urb_ctx *ctx = urb->context;
	struct sb_midex *midex = ctx->midex;
	unsigned long flags;

	if (urb->status)
		sb_midex_urb_show_error(urb, __func__);

	if (!midex) {
		dev_warn(&urb->dev->dev, SB_MIDEX_PREFIX "no midex\n");
		return;
	}

	spin_lock_irqsave(&midex->midi_out.lock, flags);
	ctx->active = false;
	spin_unlock_irqrestore(&midex->midi_out.lock, flags);

	//sb_midex_usb_midi_output(midex);
	tasklet_schedule(&midex->midi_out_tasklet);
}


static void sb_midex_usb_midi_output_tasklet(unsigned long data) {
	struct sb_midex *midex = (struct sb_midex *) data;

	sb_midex_usb_midi_output(midex);
}


static void sb_midex_usb_timing_output_complete(struct urb *urb) {
	unsigned long flags;

	struct sb_midex_urb_ctx *ctx = urb->context;
	struct sb_midex *midex = ctx->midex;

	spin_lock_irqsave(&midex->timer_timing_lock, flags);

	if (urb->status)
		sb_midex_urb_show_error(urb, __func__);

	if (!ctx) {
		dev_warn(&urb->dev->dev, SB_MIDEX_PREFIX "no midex for timing output complete");
		return;
	}

	ctx->active = false;

	spin_unlock_irqrestore(&midex->timer_timing_lock, flags);

	/* Start reading MIDI input only after the timing (start) event has been sent. */
	// moved to timing_callback()...
	//if (!midex->midi_in.active && midex->timing_state != SB_MIDEX_TIMING_IDLE)
	//	sb_midex_usb_midi_input_start(midex);
}


static void sb_midex_usb_led_output_complete(struct urb *urb) {
	struct sb_midex_urb_ctx *ctx = urb->context;
	struct sb_midex *midex = ctx->midex;
	int urb_err = 0;

	if (urb->status)
		urb_err = sb_midex_urb_show_error(urb, __func__);

	if (!ctx || !midex)
		return;

	ctx->active = false;

	if (midex->led_state == SB_MIDEX_LED_RUNNING && !urb_err) {
		/* read reply from MIDEX */
		sb_midex_submit_urb(&midex->led_replies_urb, GFP_ATOMIC, __func__);
	}
}


static void sb_midex_usb_led_input_complete(struct urb *urb) {
	struct sb_midex_urb_ctx *ctx = urb->context;

	if (urb->status)
		sb_midex_urb_show_error(urb, __func__);

	if (!ctx)
		return;

	ctx->active = false;
}


/*********************************************************************************
 * Device functions
 *********************************************************************************/
static enum hrtimer_restart sb_midex_timer_timing_callback(struct hrtimer *hrt) {
	struct sb_midex *midex = container_of(hrt, struct sb_midex, timer_timing);
	unsigned long flags;
	int i;
	unsigned char *buffer;

	spin_lock_irqsave(&midex->timer_timing_lock, flags);

	/* unlink all still active urbs, it shouldn't take 25ms to send */
	for (i = 0; i < SB_MIDEX_NUM_URBS_PER_EP; i++) {
		if (midex->timing_out_urb[i].active) {
			dev_info(&midex->usbdev->dev, SB_MIDEX_PREFIX "timing urb %d still active, unlinking", i);
			usb_unlink_urb(midex->timing_out_urb[i].urb);
		}
	}

	/* find a free urb */
	for (i = 0; i < SB_MIDEX_NUM_URBS_PER_EP && midex->timing_out_urb[i].active; i++);

	/* if we found a free urb: use it. */
	if (!midex->timing_out_urb[i].active) {
		/* always the same */
		buffer = midex->timing_out_urb[i].urb->transfer_buffer;
		buffer[0] = 0x0f;
		buffer[2] = 0;
		buffer[3] = 0;

		midex->timing_out_urb[i].urb->transfer_buffer_length = 4;

		switch(midex->timing_state)
		{
		case SB_MIDEX_TIMING_START:
			buffer[1] = 0xfd; /* start*/
			sb_midex_submit_urb(&midex->timing_out_urb[i], GFP_ATOMIC, __func__);

			midex->timing_state = SB_MIDEX_TIMING_RUNNING;
			break;
		case SB_MIDEX_TIMING_RUNNING:
			buffer[1] = 0xf9;	/* running */
			sb_midex_submit_urb(&midex->timing_out_urb[i], GFP_ATOMIC, __func__);

			if (!midex->midi_in.active && midex->timing_state != SB_MIDEX_TIMING_IDLE)
				sb_midex_usb_midi_input_start(midex);
			break;
		case SB_MIDEX_TIMING_STOP:
			buffer[1] = 0xf5; /* stop */
			sb_midex_submit_urb(&midex->timing_out_urb[i], GFP_ATOMIC, __func__);

			/* cancel outstanding MIDI-in urbs*/
			sb_midex_usb_midi_input_stop(midex);

			midex->timing_state = SB_MIDEX_TIMING_IDLE;
			break;
		default:
		case SB_MIDEX_TIMING_IDLE:
			/* do nothing */
			break;
		}
	}

	/* We want this timer to be periodic, so forward it */
	hrtimer_forward_now(&(midex->timer_timing), midex->timer_timing_deltat);

	spin_unlock_irqrestore(&midex->timer_timing_lock, flags);

	return HRTIMER_RESTART;
}


static int sb_midex_usb_led_fill_and_send_command(
		struct sb_midex_urb_ctx* ctx,
		unsigned char led_nr,
		bool		  led_state,
		bool		  led_is_left)
{
	unsigned char* buf = ctx->urb->transfer_buffer;

	buf[0] = 0x40;
	buf[1] = (0x44 | (led_nr << 3));
	if (led_state) {
		buf[2] = 0xff;
		buf[3] = led_is_left ? 0x02 : 0x01;
	} else {
		buf[2] = 0xfc;
		buf[3] = 0x0;
	}

	ctx->urb->transfer_buffer_length = 4;

	return sb_midex_submit_urb(ctx, GFP_ATOMIC, __func__);
}


static void sb_midex_timer_led_callback(unsigned long data)
{
	struct sb_midex *midex = (struct sb_midex *) data;
	unsigned char* buffer;
	int ret;
	unsigned char led_nr;

	/* We want this timer to be periodic... */
	if (midex->timing_state != SB_MIDEX_TIMING_IDLE)
		mod_timer(&(midex->timer_led), jiffies + msecs_to_jiffies(TIMER_PERIOD_LED_WHEN_ACTIVE_MS));
	else
		mod_timer(&(midex->timer_led), jiffies + msecs_to_jiffies(TIMER_PERIOD_LED_WHEN_INACTIVE_MS));


	// check if the previously sent urb was still active... it shouldn't be afte 50+ms, but it happens.
	if (midex->led_replies_urb.active || midex->led_commands_urb[0].active) {
		if (midex->led_commands_urb[0].active) {
			dev_info(&midex->usbdev->dev, SB_MIDEX_PREFIX "led reply urb still active, unlinking");
			usb_unlink_urb(midex->led_commands_urb[0].urb);
		}
		if (midex->led_replies_urb.active) {
			dev_info(&midex->usbdev->dev, SB_MIDEX_PREFIX "led reply urb still active, unlinking");
			usb_unlink_urb(midex->led_replies_urb.urb);
		}
		return; // try again next time...
	}

	switch(midex->led_state) {
	default:
	case SB_MIDEX_LED_RUNNING:
		buffer = midex->led_commands_urb[0].urb->transfer_buffer;
		buffer[0] = 0x7f;
		buffer[1] = 0x9a;
		midex->led_commands_urb[0].urb->transfer_buffer_length = 2;

		ret = sb_midex_submit_urb(&midex->led_commands_urb[0], GFP_ATOMIC, __func__);
		break;
	case SB_MIDEX_LED_INIT: /* start of device */
		/* read reply from MIDEX */
		sb_midex_submit_urb(&midex->led_replies_urb, GFP_ATOMIC, __func__);
		midex->led_state = SB_MIDEX_LED_GFX_RUN_OUT;
		break;

	case SB_MIDEX_LED_GFX_RUN_OUT: /* turn single led on from outer to inner */
		if (midex->led_state_gfx > 0) {
			// off the previous led
			led_nr = midex->led_state_gfx - 1;
			sb_midex_usb_led_fill_and_send_command(&midex->led_commands_urb[0],     led_nr, false, true);
			sb_midex_usb_led_fill_and_send_command(&midex->led_commands_urb[1], 7 - led_nr, false, false);
		}
		if (midex->led_state_gfx < 8) {
			led_nr = midex->led_state_gfx;
			sb_midex_usb_led_fill_and_send_command(&midex->led_commands_urb[2],     led_nr,	true, true);
			sb_midex_usb_led_fill_and_send_command(&midex->led_commands_urb[3], 7 - led_nr,	true, false);
		}

		if (midex->led_state_gfx >= 8) {
			midex->led_state = SB_MIDEX_LED_GFX_FILL_IN; /* goto next graphics state */
			midex->led_state_gfx = 7;
		} else {
			midex->led_state_gfx++;
		}
		break;

	case SB_MIDEX_LED_GFX_FILL_IN: /* turn leds on from inner to outer */
		if (midex->led_state_gfx >= 0) {
			led_nr = midex->led_state_gfx;
			sb_midex_usb_led_fill_and_send_command(&midex->led_commands_urb[0],     led_nr, true, true);
			sb_midex_usb_led_fill_and_send_command(&midex->led_commands_urb[1], 7 - led_nr, true, false);
		}
		if (midex->led_state_gfx < -2) {
			midex->led_state = SB_MIDEX_LED_GFX_RUN_IN; /* goto next graphics state */
			midex->led_state_gfx = 7;
		} else {
			midex->led_state_gfx--;
		}
		break;

	case SB_MIDEX_LED_GFX_RUN_IN: /* turn leds off from inner to outer */
		if (midex->led_state_gfx >= 0) {
			led_nr = midex->led_state_gfx;
			sb_midex_usb_led_fill_and_send_command(&midex->led_commands_urb[0],     led_nr, false, true);
			sb_midex_usb_led_fill_and_send_command(&midex->led_commands_urb[1], 7 - led_nr, false, false);
		}
		if (midex->led_state_gfx < 0) {
			midex->led_state = SB_MIDEX_LED_RUNNING; /* Done with the fancy graphics */
			midex->led_state_gfx = 0;
		} else {
			midex->led_state_gfx--;
		}
		break;
	}
}


void sb_midex_timer_timing_start(struct sb_midex *midex) {
	hrtimer_init(&(midex->timer_timing), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	midex->timer_timing.function = sb_midex_timer_timing_callback;

	midex->timer_timing_deltat = ktime_set(0, TIMER_PERIOD_TIMING_NS);
	hrtimer_start(&(midex->timer_timing), midex->timer_timing_deltat, HRTIMER_MODE_REL);
}


void sb_midex_timer_led_start(struct sb_midex *midex) {
	setup_timer(&(midex->timer_led), sb_midex_timer_led_callback, (unsigned long)midex);
	mod_timer(&(midex->timer_led), jiffies + msecs_to_jiffies(TIMER_PERIOD_LED_WHEN_INACTIVE_MS));
}


/**
 * Init the device by sending some specific commands
 *
 * \pre init_rawmidi and init_usb were successful
 */
static int sb_midex_init_device(struct sb_midex *midex) {
	unsigned char* buffer;
	int err = 0;

	init_usb_anchor(&midex->anchor);
	usb_anchor_urb(midex->led_replies_urb.urb, &midex->anchor);

	/* Start reading EP6in(led_reply) */
	err = sb_midex_submit_urb(&midex->led_replies_urb, GFP_KERNEL, __func__);
	if (err < 0)
		goto init_device_error;

	/* wait for it to complete (should get empty reply) */
	usb_wait_anchor_empty_timeout(&midex->anchor, 1000);

	/* Then send [FE 01]*/
	midex->led_num_packets_to_send = 0;
	buffer = midex->led_commands_urb[0].urb->transfer_buffer;
	buffer[0] = 0xfe;
	buffer[1] = 0x01;
	midex->led_commands_urb[0].urb->transfer_buffer_length = 2;

	err = sb_midex_submit_urb(&midex->led_commands_urb[0], GFP_KERNEL, __func__);
	if (err < 0)
		goto init_device_error;

	/* after this, the timers handle the rest */
	/* start timer for EP6out(LED) */
	sb_midex_timer_led_start(midex);
	/* start timer for EP2out(timing) */
	sb_midex_timer_timing_start(midex);

init_device_error:
	return err;
}

/**
 * Allocate urbs, allocate dma buffers and fill the urb with the right data
 */
static int sb_midex_init_usb(struct sb_midex* midex) {
	int i;
	int err = usb_set_interface(midex->usbdev, 0, 0);

	if (err < 0) {
		dev_err(&midex->usbdev->dev, SB_MIDEX_PREFIX "usb_set_interface failed\n");
		return err;
	}

	/* alloc urbs */
	midex->led_replies_urb.urb = sb_midex_urb_and_buffer_alloc(
			midex, usb_rcvintpipe(midex->usbdev, 0x86),	8,
			sb_midex_usb_led_input_complete, &midex->led_replies_urb);

	if (!midex->led_replies_urb.urb)
		goto init_usb_error;

	for (i = 0; i < SB_MIDEX_NUM_URBS_PER_EP; ++i) {
		midex->led_commands_urb[i].urb = sb_midex_urb_and_buffer_alloc(
				midex, usb_sndintpipe(midex->usbdev, 0x06), 8,
				sb_midex_usb_led_output_complete, &midex->led_commands_urb[i]);
		if (!midex->led_commands_urb[i].urb)
			goto init_usb_error;

		midex->timing_out_urb[i].urb = sb_midex_urb_and_buffer_alloc(
				midex, usb_sndintpipe(midex->usbdev, 0x02),	SB_MIDEX_URB_BUFFER_SIZE,
				sb_midex_usb_timing_output_complete, &midex->timing_out_urb[i]);
		if (!midex->timing_out_urb[i].urb)
			goto init_usb_error;

		midex->midi_in.urbs[i].urb = sb_midex_urb_and_buffer_alloc(
				midex, usb_rcvintpipe(midex->usbdev, 0x82),	SB_MIDEX_URB_BUFFER_SIZE,
				sb_midex_usb_midi_input_complete, &midex->midi_in.urbs[i]);
		if (!midex->midi_in.urbs[i].urb)
			goto init_usb_error;

		midex->midi_out.urbs[i].urb = sb_midex_urb_and_buffer_alloc(
				midex, usb_sndintpipe(midex->usbdev, 0x04),	SB_MIDEX_URB_BUFFER_SIZE,
				sb_midex_usb_midi_output_complete, &midex->midi_out.urbs[i]);
		if (!midex->midi_out.urbs[i].urb)
			goto init_usb_error;
	}

	return 0;
init_usb_error:
	dev_err(&midex->usbdev->dev, SB_MIDEX_PREFIX "usb_alloc_urb failed\n");
	return -ENOMEM;
}


/**
 * Get the input/output substreams and store them in our ports list.
 * Also set the substream name
 */
static void sb_midex_init_rawmidi_substreams(struct sb_midex *midex) {
	struct snd_rawmidi_substream *substream;
	/* set name and reference in midex struct */

	list_for_each_entry(
			substream,
			&midex->rmidi->streams[SNDRV_RAWMIDI_STREAM_OUTPUT].substreams,
			list)
	{
		midex->midi_out.ports[substream->number].substream = substream;
		snprintf(substream->name, sizeof(substream->name),
				"MIDEX Port %d", substream->number + 1);
	}

	list_for_each_entry(
			substream,
			&midex->rmidi->streams[SNDRV_RAWMIDI_STREAM_INPUT].substreams,
			list)
	{
		midex->midi_in.ports[substream->number].substream = substream;
		snprintf(substream->name, sizeof(substream->name),
				"MIDEX Port %d", substream->number + 1);
	}
}


static int sb_midex_init_rawmidi(struct sb_midex *midex) {
	int ret;
	struct snd_rawmidi *rmidi;

	switch(midex->card_type) {
		case SB_MIDEX_TYPE_8:
			midex->midi_in.num_ports = 8;
			midex->midi_out.num_ports = 8;
			break;
		case SB_MIDEX_TYPE_3:
			midex->midi_in.num_ports = 1;
			midex->midi_out.num_ports = 3;
			break;
		default:
			midex->midi_in.num_ports = 0;
			midex->midi_out.num_ports = 0;
			break;
	}

	ret = snd_rawmidi_new(
			midex->card,
			midex->card->shortname,
			0,
			midex->midi_out.num_ports, /* #outputs */
			midex->midi_in.num_ports, /* #inputs */
			&rmidi);

	if (ret < 0)
		return ret;

	strlcpy(rmidi->name, midex->card->shortname, sizeof(rmidi->name));

	rmidi->info_flags = SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = midex;

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &sb_midex_raw_midi_output);

	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_INPUT;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &sb_midex_raw_midi_input);

	midex->rmidi = rmidi;

	sb_midex_init_rawmidi_substreams(midex);

	return 0;
}


/**
 * Determine what type of MIDEX is connected.
 * Depending on PID we can determine what firmware version the device is running,
 * however, firmware updates are not supported at the moment.
 *
 * Maybe the MIDEX3 has the same protocol as the MIDEX8, let me know if that is the case and what PID
 */
static void sb_midex_init_determine_type_and_name(struct sb_midex* midex) {
	char usb_path[32];

	usb_make_path(midex->usbdev, usb_path, sizeof(usb_path));

	switch(le16_to_cpu(midex->usbdev->descriptor.idProduct)) {
		case SB_MIDEX8_PID1:
			dev_info(&midex->usbdev->dev, SB_MIDEX_PREFIX "Firmware update not implemented.");
		case SB_MIDEX8_PID2:
			/* Let USB descriptor tell us what name this device has: */
			strncpy(midex->card->shortname, midex->usbdev->product, sizeof(midex->card->shortname));
			midex->card_type = SB_MIDEX_TYPE_8;
			dev_info(&midex->usbdev->dev, SB_MIDEX_PREFIX "Recognized MIDEX8 at %s", usb_path);
			break;
		default:
			/* Figure out if its a midex3, what PID...*/
			strncpy(midex->card->shortname, "MIDEXxxx", sizeof(midex->card->shortname));
			midex->card_type = SB_MIDEX_TYPE_UNKNOWN;
			break;
	}

	snprintf(midex->card->longname, sizeof(midex->card->longname), "%s at %s",
			midex->usbdev->product, /* product string as given by device */
			usb_path);
}


static void sb_midex_init_midex_urb(struct sb_midex *midex, struct sb_midex_urb_ctx *urbctx) {
	urbctx->active = false;
	urbctx->urb = NULL;
	urbctx->midex = midex;
}


static struct sb_midex* sb_midex_init_midex_data_struct(
	struct usb_interface* interface,
	struct snd_card* card,
	unsigned int card_index)
{
	struct sb_midex* midex;
	int i;

	midex = card->private_data;
	midex->card = card;
	midex->card_index = card_index;
	midex->usbdev = interface_to_usbdev(interface);
	midex->intf = interface;

	midex->num_used_substreams = 0;
	midex->timing_state = SB_MIDEX_TIMING_IDLE;

	midex->led_state = SB_MIDEX_LED_INIT; /* start state */
	midex->led_state_gfx = 0;

	midex->midi_in.active = false;
	midex->midi_out.active = false;

	spin_lock_init(&(midex->midi_out.lock));
	spin_lock_init(&(midex->midi_in.lock));
	spin_lock_init(&(midex->timer_timing_lock));

	tasklet_init(&midex->midi_out_tasklet, sb_midex_usb_midi_output_tasklet, (unsigned long)midex);

	/* clear ports mem */
	for (i = 0; i < 8; ++i) {
		midex->midi_in.ports[i].substream = NULL;
		midex->midi_in.ports[i].triggered = 0;
		midex->midi_in.ports[i].state = STATE_UNKNOWN;

		midex->midi_out.ports[i].substream = NULL;
		midex->midi_out.ports[i].triggered = 0;
		midex->midi_out.ports[i].state = STATE_UNKNOWN;
	}

	/* clear urb ctx mem */
	sb_midex_init_midex_urb(midex, &midex->led_replies_urb);

	for (i = 0; i < SB_MIDEX_NUM_URBS_PER_EP; ++i) {
		sb_midex_init_midex_urb(midex, &midex->led_commands_urb[i]);
		sb_midex_init_midex_urb(midex, &midex->timing_out_urb[i]);
		sb_midex_init_midex_urb(midex, &midex->midi_in.urbs[i]);
		sb_midex_init_midex_urb(midex, &midex->midi_out.urbs[i]);
	}

	return midex;
}


static void sb_midex_free_usb_related_resources(struct sb_midex *midex,	struct usb_interface *interface) {
	int i;
	/* usb_kill_urb not necessary, urb is aborted automatically */

	sb_midex_urb_and_buffer_free(midex, midex->led_replies_urb.urb, 8);

	for (i = 0; i < SB_MIDEX_NUM_URBS_PER_EP; ++i) {
		sb_midex_urb_and_buffer_free(midex, midex->led_commands_urb[i].urb, 8);
		sb_midex_urb_and_buffer_free(midex, midex->timing_out_urb[i].urb, SB_MIDEX_URB_BUFFER_SIZE);
		sb_midex_urb_and_buffer_free(midex, midex->midi_in.urbs[i].urb, SB_MIDEX_URB_BUFFER_SIZE);
		sb_midex_urb_and_buffer_free(midex, midex->midi_out.urbs[i].urb, SB_MIDEX_URB_BUFFER_SIZE);
	}

	if (midex->intf) {
		usb_set_intfdata(midex->intf, NULL);
		midex->intf = NULL;
	}
}


/*********************************************************************************
 * Module functions
 *********************************************************************************/

static int sb_midex_init_driver(struct sb_midex *midex) {
	int ret;

	sb_midex_init_determine_type_and_name(midex);

	ret = sb_midex_init_rawmidi(midex);
	if (ret < 0)
		return ret;

	ret = sb_midex_init_usb(midex);
	if (ret < 0)
		return ret;

	ret = sb_midex_init_device(midex);
	if (ret < 0)
		return ret;

	return 0;
}


static int sb_midex_drv_probe(struct usb_interface *interface, const struct usb_device_id *usb_id) {
	struct snd_card *card;
	struct sb_midex* midex;
	unsigned int card_index;
	int err;

	mutex_lock(&devices_mutex);

	for (card_index = 0; card_index < SNDRV_CARDS; ++card_index)
		if (!test_bit(card_index, devices_used))
			break;

	if (card_index >= SNDRV_CARDS) {
		mutex_unlock(&devices_mutex);
		return -ENOENT;
	}

	err = snd_card_new(
			&interface->dev,
			index[card_index],
			id[card_index],
			THIS_MODULE,
			sizeof(struct sb_midex), // sizeof(*midex), // or that one??
			&card);

	if (err < 0) {
		mutex_unlock(&devices_mutex);
		return err;
	}

	midex = sb_midex_init_midex_data_struct(interface, card, card_index);
	if (midex == NULL) {
		err = -ENOMEM;
		goto probe_error;
	}

	err = sb_midex_init_driver(midex);
	if (err < 0)
		goto probe_error;

	err = snd_card_register(card);
	if (err < 0)
		goto probe_error;

	snd_card_set_dev(card, &interface->dev);
	strncpy(card->driver, "snd-usb-midex", sizeof(card->driver));

	usb_set_intfdata(interface, midex);
	set_bit(card_index, devices_used);

	mutex_unlock(&devices_mutex);
	return 0;

probe_error:
	dev_info(&midex->usbdev->dev, SB_MIDEX_PREFIX "error during probing");
	sb_midex_free_usb_related_resources(midex, interface);
	snd_card_free(card);
	mutex_unlock(&devices_mutex);
	return err;
}


static void sb_midex_drv_disconnect(struct usb_interface *interface) {
	struct sb_midex *midex = usb_get_intfdata(interface);

	if (!midex)
		return;

	del_timer_sync(&(midex->timer_led));
	hrtimer_cancel(&(midex->timer_timing));
	tasklet_kill(&(midex->midi_out_tasklet));

	mutex_lock(&devices_mutex);

	/* make sure that userspace cannot create new requests */
	snd_card_disconnect(midex->card);

	sb_midex_free_usb_related_resources(midex, interface);

	clear_bit(midex->card_index, devices_used);

	snd_card_free_when_closed(midex->card);

	mutex_unlock(&devices_mutex);
}


static struct usb_driver sb_midex_driver = {
	.name =			"snd-usb-midex",
	.probe =		sb_midex_drv_probe,
	.disconnect =	sb_midex_drv_disconnect,
	.id_table =		id_table,
};


module_usb_driver(sb_midex_driver);

MODULE_DEVICE_TABLE(usb, id_table);
MODULE_AUTHOR("Hedde Bosman, sgorpi@gmail.com");
MODULE_DESCRIPTION("Steinberg MIDEX driver");
MODULE_LICENSE("GPL");
