/*
 * thread_ep6.c
 *
 *  Created on: Apr 30, 2017
 *      Author: hedde
 */

#include "threads.h"

#define SPLASH_DELAY_MS 50

void show_led_splash(struct libusb_device_handle *devh) {
	int r;
	uint8_t buffer[8];
	int num_transfered;

	buffer[0] = 0x40;

	// 1 on from out to in
	for (int8_t n = 7; n >= 0; n--) {
		buffer[2] = 0xff; // on

		buffer[1] = 0x44 | (n<<3);
		buffer[3] = 02;

		r = libusb_interrupt_transfer(devh, 0x06 | LIBUSB_ENDPOINT_OUT, buffer, 4, &num_transfered, /*timeout:*/0);
		check_for_error(r, "Error send IR transfer. %s\n", devh);

		buffer[1] = 0x44 | ((7-n)<<3);
		buffer[3] = 01;

		r = libusb_interrupt_transfer(devh, 0x06 | LIBUSB_ENDPOINT_OUT, buffer, 4, &num_transfered, /*timeout:*/0);
		check_for_error(r, "Error send IR transfer. %s\n", devh);

		usleep(SPLASH_DELAY_MS*1000);

		buffer[2] = 0xfc; // off
		buffer[3] = 00;

		buffer[1] = 0x44 | (n<<3);

		r = libusb_interrupt_transfer(devh, 0x06 | LIBUSB_ENDPOINT_OUT, buffer, 4, &num_transfered, /*timeout:*/0);
		check_for_error(r, "Error send IR transfer. %s\n", devh);

		buffer[1] = 0x44 | ((7-n)<<3);

		r = libusb_interrupt_transfer(devh, 0x06 | LIBUSB_ENDPOINT_OUT, buffer, 4, &num_transfered, /*timeout:*/0);
		check_for_error(r, "Error send IR transfer. %s\n", devh);
	}

	// keep on from in to out
	for (int8_t n = 0; n < 8; n++) {
		buffer[2] = 0xff; // on

		buffer[1] = 0x44 | (n<<3);
		buffer[3] = 02;

		r = libusb_interrupt_transfer(devh, 0x06 | LIBUSB_ENDPOINT_OUT, buffer, 4, &num_transfered, /*timeout:*/0);
		check_for_error(r, "Error send IR transfer. %s\n", devh);

		buffer[1] = 0x44 | ((7-n)<<3);
		buffer[3] = 01;

		r = libusb_interrupt_transfer(devh, 0x06 | LIBUSB_ENDPOINT_OUT, buffer, 4, &num_transfered, /*timeout:*/0);
		check_for_error(r, "Error send IR transfer. %s\n", devh);

		usleep(SPLASH_DELAY_MS*1000);
	}

	// all off
	for (int8_t n = 0; n < 8; n++) {
		buffer[2] = 0xfc; // on
		buffer[3] = 00;

		buffer[1] = 0x44 | (n<<3);

		r = libusb_interrupt_transfer(devh, 0x06 | LIBUSB_ENDPOINT_OUT, buffer, 4, &num_transfered, /*timeout:*/0);
		check_for_error(r, "Error send IR transfer. %s\n", devh);

		buffer[1] = 0x44 | ((7-n)<<3);

		r = libusb_interrupt_transfer(devh, 0x06 | LIBUSB_ENDPOINT_OUT, buffer, 4, &num_transfered, /*timeout:*/0);
		check_for_error(r, "Error send IR transfer. %s\n", devh);

		usleep(SPLASH_DELAY_MS*1000);
	}
}


void* thread_ep6_leds(void* data) {
	int r;
	uint8_t buffer[8];
	int num_transfered;
	struct libusb_device_handle *devh = (struct libusb_device_handle *) data;

	/* Assume libusb and the usb device are initialized etc so we can work with them... */
	printf("Thread EP6\n");
//- EP 6 in: IR, empty
//- EP 6 out: IR, host sends: [fe 01]
//- EP 6 in: IR, midex sends: [01]
//- sleep
//- EP 6 out: send LED graphics
//- loop:
//-   EP 6 : send [7f 9a] - receive [2f]

	r = libusb_interrupt_transfer(devh, 0x06 | LIBUSB_ENDPOINT_IN, buffer, 0, &num_transfered, /*timeout:*/0);
	check_for_error(r, "Error recv IR transfer. %s\n", devh);

	buffer[0] = 0xfe;
	buffer[1] = 0x01;
	r = libusb_interrupt_transfer(devh, 0x06 | LIBUSB_ENDPOINT_OUT, buffer, 2, &num_transfered, /*timeout:*/0);
	check_for_error(r, "Error send IR transfer. %s\n", devh);

	r = libusb_interrupt_transfer(devh, 0x06 | LIBUSB_ENDPOINT_IN, buffer, 1, &num_transfered, /*timeout:*/50);
	check_for_error(r, "Error recv IR transfer. %s\n", devh);


	show_led_splash(devh);

	while (!do_exit) {
		//send [7f 9a]
		buffer[0] = 0x7f;
		buffer[1] = 0x9a;
		r = libusb_interrupt_transfer(devh, 0x06 | LIBUSB_ENDPOINT_OUT, buffer, 2, &num_transfered, /*timeout:*/0);
		check_for_error(r, "Error send IR transfer. %s\n", devh);

		//receive [2f]
		r = libusb_interrupt_transfer(devh, 0x06 | LIBUSB_ENDPOINT_IN, buffer, 1, &num_transfered, /*timeout:*/50);
		if (r == LIBUSB_ERROR_TIMEOUT)
			printf("EP6 timeout error\n");
		else
			check_for_error(r, "Error recv IR transfer. %s\n", devh);

		usleep(50*1000);
	}
	pthread_exit(NULL);
}
