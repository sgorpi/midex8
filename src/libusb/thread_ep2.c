/*
 * thread_ep2.c
 *
 *  Created on: Apr 30, 2017
 *      Author: hedde
 *
 * send regular messages
 */

#include "threads.h"
#include <string.h>

//- EP 2 out: IR, host sends [0f fd 00 00]
//- EP 2 out: IR, host sends [0f f9 00 00]
//- EP 2 out: IR, host sends [0f f9 00 00]
//- loop:  EP 2 out continues to send regular [0f f9 00 00]
//- last EP 2 out before windows shuts down: IR, host sends [0f f5 00 00]

void* thread_ep2_in_midi(void* data) {
	int r;
	uint8_t buffer[16];
	int num_transfered;
	struct libusb_device_handle *devh = (struct libusb_device_handle *) data;

	/* Assume libusb and the usb device are initialized etc so we can work with them... */
	printf("Thread EP2 in\n");

	while (!do_exit) {
		memset(buffer, 0, 16);

		r = libusb_interrupt_transfer(devh, 0x02 | LIBUSB_ENDPOINT_IN, buffer, 16, &num_transfered, /*timeout:*/1000);
		if (r != LIBUSB_ERROR_TIMEOUT)
			check_for_error(r, "Error recv IR transfer. %s\n", devh);

		if (num_transfered > 0)
			print_midex_messages(buffer, num_transfered);
	}

	pthread_exit(NULL);
}


void* thread_ep2_out_timing(void* data) {
	int r;
	uint8_t buffer[4];
	int num_transfered;
	struct libusb_device_handle *devh = (struct libusb_device_handle *) data;

	/* Assume libusb and the usb device are initialized etc so we can work with them... */
	printf("Thread EP2 out\n");


	buffer[0] = 0x0f;
	buffer[1] = 0xfd;
	buffer[2] = 0;
	buffer[3] = 0;
	r = libusb_interrupt_transfer(devh, 0x02 | LIBUSB_ENDPOINT_OUT, buffer, 4, &num_transfered, /*timeout:*/0);
	check_for_error(r, "Error send IR transfer. %s\n", devh);

	while (!do_exit) {
		// the out transfer should always be every 25 ms, regardless of the input transfer!
		buffer[0] = 0x0f;
		buffer[1] = 0xf9;
		buffer[2] = 0;
		buffer[3] = 0;
		r = libusb_interrupt_transfer(devh, 0x02 | LIBUSB_ENDPOINT_OUT, buffer, 4, &num_transfered, /*timeout:*/0);
		check_for_error(r, "Error send IR transfer. %s\n", devh);

		usleep(50*1000);
	}

	buffer[0] = 0x0f;
	buffer[1] = 0xf5;
	buffer[2] = 0;
	buffer[3] = 0;
	r = libusb_interrupt_transfer(devh, 0x02 | LIBUSB_ENDPOINT_OUT, buffer, 4, &num_transfered, /*timeout:*/0);
	check_for_error(r, "Error send IR transfer. %s\n", devh);

	pthread_exit(NULL);
}
