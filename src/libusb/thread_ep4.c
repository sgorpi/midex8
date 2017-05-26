/*
 * thread_ep4.c
 *
 *  Created on: May 1, 2017
 *      Author: hedde
 */

#include "threads.h"


void* thread_ep4_out_midi(void* data) {
	int r;
	uint8_t buffer[16];
	int num_transfered;
	struct libusb_device_handle *devh = (struct libusb_device_handle *) data;

	uint8_t port = 2;

	printf("Thread EP4 out\n");

	while (!do_exit) {
		uint8_t note = 0;
		int input;

		input = getchar();
		switch(input) {
		case 'a':	note = 0; break; // c
		case 'w':	note = 1; break;
		case 's':	note = 2; break;
		case 'e':	note = 3; break;
		case 'd':	note = 4; break;
		case 'f':	note = 5; break;
		case 't':	note = 6; break;
		case 'g':	note = 7; break;
		case 'y':	note = 8; break;
		case 'h':	note = 9; break;
		case 'u':	note = 10; break;
		case 'j':	note = 11; break;
		case 'k':	note = 12; break;
		case 'l':	note = 13; break; // c
		default:
			continue;
		}
		note += 60;

		// send note on
		buffer[1] = 0x90 | 0x00; // message+channel
		buffer[2] = note;
		buffer[3] = 0x70; // velocity
		buffer[0] = ((port & 0x0f) << 4) | (buffer[1] >> 4);

		r = libusb_interrupt_transfer(devh, 0x04 | LIBUSB_ENDPOINT_OUT, buffer, 4, &num_transfered, /*timeout:*/0);
		check_for_error(r, "Error send IR transfer. %s\n", devh);

		printf("Sent note %d on\n", note);


		usleep(100*1000);
		// send note off
		buffer[1] = 0x80 | 0x00; // message+channel
		buffer[3] = 0x00; // velocity
		buffer[0] = ((port & 0x0f) << 4) | (buffer[1] >> 4);

		r = libusb_interrupt_transfer(devh, 0x04 | LIBUSB_ENDPOINT_OUT, buffer, 4, &num_transfered, /*timeout:*/0);
		check_for_error(r, "Error send IR transfer. %s\n", devh);

		printf("Sent note %d off\n", note);
	}


	return NULL;
}
