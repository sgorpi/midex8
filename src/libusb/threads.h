/*
 * threads.h
 *
 *  Created on: Apr 30, 2017
 *      Author: hedde
 */

#ifndef SRC_POC_THREADS_H_
#define SRC_POC_THREADS_H_

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <libusb.h>


extern int do_exit;

void* thread_ep2_in_midi(void* data);
void* thread_ep2_out_timing(void* data);
void* thread_ep4_out_midi(void* data);
void* thread_ep6_leds(void* data);

void check_for_error(int r, const char* msg, struct libusb_device_handle *devh);

void print_midex_messages(uint8_t* buffer, int len);


#endif /* SRC_POC_THREADS_H_ */
