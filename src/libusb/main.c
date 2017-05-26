/*
 * main.c
 *
 *  Created on: Apr 30, 2017
 *      Author: hedde
 */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#include "threads.h"

#define MIDEX_VID		0x0a4e
#define MIDEX_PID		0x1010
#define MIDEX_PID_ALT	0x1001

int do_exit = 0;

pthread_t tr_ep2_in;
pthread_t tr_ep2_out;
pthread_t tr_ep6;

void print_midex_message(uint8_t* buffer) {
	uint8_t port = (buffer[0] >> 4);
	uint8_t status = buffer[0] & 0x0f;

	printf("%02X %02X %02X %02X: ", buffer[0], buffer[1], buffer[2], buffer[3]);
	switch(status) {
	case 0x03: // time
		if (buffer[1] == 0xf4) {
			uint16_t time = (buffer[2] << 8) | buffer[3];
			printf("@Port %d: @TIME [%d]", port, time);
		} else {
			printf("@Port %d: @UNKNOWN: [%02X %02X %02X]", port, buffer[1], buffer[2], buffer[3]);
		}
		break;
	case 0x04: // sysex
		printf("@Port %d: SYSEX [%02X %02X %02X]", port, buffer[1], buffer[2], buffer[3]);
		break;
	case 0x05: // sysex end@ 1
		printf("@Port %d: SYSEX [%02X      ]", port, buffer[1]);
		break;
	case 0x06: // sysex end@ 2
		printf("@Port %d: SYSEX [%02X %02X   ]", port, buffer[1], buffer[2]);
		break;
	case 0x07: // sysex end@ 3
		printf("@Port %d: SYSEX [%02X %02X %02X]", port, buffer[1], buffer[2], buffer[3]);
		break;
	default: // midi?
		if ((buffer[1] & 0xf0) != (status<<4)) {
			printf("status error");
		}
		printf("@Port %d: MIDI  [%02X %02X %02X]", port, buffer[1], buffer[2], buffer[3]);
		break;
	}
	printf("\n");
	fflush(stdout);
}

void print_midex_messages(uint8_t* buffer, int len) {
	while (len >= 4) {
		print_midex_message(buffer);
		len -= 4;
		buffer += 4;
	}
}


static void sighandler(int signum) {
	do_exit = 1;
}


void do_clean_exit(int code, struct libusb_device_handle *devh) {
	if (devh != NULL)
		libusb_close(devh);

	libusb_exit(NULL);

	exit(code);
}

void check_for_error(int r, const char* msg, struct libusb_device_handle *devh) {
	if (r < 0) {
		fprintf(stderr, msg,  libusb_strerror(r));
		do_clean_exit(1, devh);
	}
}


void show_readable_device_info(
		struct libusb_device_handle *devh)
{
	struct libusb_device_descriptor  devDesc;
	unsigned char              strDesc[256];
	int r = 1;

	r = libusb_get_device_descriptor (libusb_get_device(devh), &devDesc);
	if (r < 0) {
		fprintf(stderr, "failed to get device descriptor. %s\n",  libusb_strerror(r));
		return;
	}

	if (devDesc.iManufacturer > 0)
	{
		r = libusb_get_string_descriptor_ascii(devh, devDesc.iManufacturer, strDesc, 256);
		if (r < 0) {
			fprintf(stderr, "failed to get manufacturer string. %s\n",  libusb_strerror(r));
			return;
		} else {
			printf("Manufacturer: %s\n", strDesc);
		}
	}

	if (devDesc.iProduct > 0)
	{
		r = libusb_get_string_descriptor_ascii(devh, devDesc.iProduct, strDesc, 256);
		if (r < 0) {
			fprintf(stderr, "failed to get product string. %s\n",  libusb_strerror(r));
			return;
		} else {
			printf("Product:      %s\n", strDesc);
		}
	}

	if (devDesc.iSerialNumber > 0)
	{
		r = libusb_get_string_descriptor_ascii(devh, devDesc.iSerialNumber, strDesc, 256);
		if (r < 0) {
			fprintf(stderr, "failed to get serial string. %s\n",  libusb_strerror(r));
			return;
		} else {
			printf("Serial:       %s\n", strDesc);
		}
	}

}


void setup_sigact() {
	struct sigaction sigact;

	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
}


void start_threads(struct libusb_device_handle *devh) {
	int r;

	r = pthread_create(&tr_ep6, NULL, thread_ep6_leds, (void*)devh);
	if (r) {
		fprintf(stderr, "* pthread_create() for ep6. Code: %d", r);
	}

	r = pthread_create(&tr_ep2_out, NULL, thread_ep2_out_timing, (void*)devh);
	if (r) {
		fprintf(stderr, "* pthread_create() for ep2 in. Code: %d", r);
	}

	r = pthread_create(&tr_ep2_in, NULL, thread_ep2_in_midi, (void*)devh);
	if (r) {
		fprintf(stderr, "* pthread_create() for ep2 in. Code: %d", r);
	}
}

void wait_for_threads() {
	pthread_join(tr_ep6, NULL);
	pthread_join(tr_ep2_out, NULL);
	pthread_join(tr_ep2_in, NULL);
}


int main(int argc, char *argv[])
{
	struct libusb_device_handle *devh = NULL;
	int r = 1;

	r = libusb_init(NULL);
	check_for_error(r, "failed to init\n", NULL);

	libusb_set_debug(NULL, LIBUSB_LOG_LEVEL_WARNING);
	//libusb_set_debug(NULL, LIBUSB_LOG_LEVEL_DEBUG);

	devh = libusb_open_device_with_vid_pid(NULL, (uint16_t)MIDEX_VID, (uint16_t)MIDEX_PID);
	if (devh == NULL) {
		fprintf(stderr, "failed to open device with PID %02X, trying with PID %X\n", MIDEX_PID, MIDEX_PID_ALT);

		devh = libusb_open_device_with_vid_pid(NULL, (uint16_t)MIDEX_VID, (uint16_t)MIDEX_PID_ALT);
		if (devh == NULL) {
			fprintf(stderr, "failed to open device with PID %X...\n", MIDEX_PID_ALT);
			do_clean_exit(1, NULL);
		}
	}


	r = libusb_kernel_driver_active(devh, 0);
	if (r > 0) {
		r = libusb_detach_kernel_driver(devh, 0);
		check_for_error(r, "failed to detach kernel driver. %s\n", devh);
	} else {
		check_for_error(r, "failed to determine active kernel driver. %s\n", devh);
	}

	show_readable_device_info(devh);

	r = libusb_claim_interface(devh, 0);
	check_for_error(r, "failed to claim interface. %s\n", devh);

	////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////
	printf("interface claimed, starting actions...\n");

	setup_sigact();

	start_threads(devh);

	thread_ep4_out_midi(devh);

	printf("\nWaiting for threads...\n");
	wait_for_threads();

	printf("Ok, done.\n\n");

	////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////

	r = libusb_release_interface(devh, 0);
	check_for_error(r, "failed to release interface. %s\n", NULL);

	do_clean_exit(0, devh);
	return 0;
}
