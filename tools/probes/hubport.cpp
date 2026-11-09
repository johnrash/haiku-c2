/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * hubport <hub, e.g. /dev/bus/usb/0/0/hub> <port from 0>: switches off the
 * power of a hub's port, as over-current protection does. The USB stack sees
 * the device go and powers the port again at its next explore, so this is an
 * unplugging and plugging in again without touching anything.
 */
#include <stdio.h>
#include <stdlib.h>

#include <USBKit.h>


int
main(int argc, char** argv)
{
	if (argc < 3) {
		printf("usage: hubport <hub> <port from 0>\n");
		return 1;
	}

	BUSBDevice hub(argv[1]);
	if (hub.InitCheck() != B_OK) {
		printf("hubport: can't open %s\n", argv[1]);
		return 1;
	}

	// class request to a port: CLEAR_FEATURE, PORT_POWER
	ssize_t result = hub.ControlTransfer(0x23, 0x01, 8, atoi(argv[2]) + 1, 0,
		NULL);
	printf("port %s: %s\n", argv[2], result >= 0 ? "power off" : "failed");
	return result >= 0 ? 0 : 1;
}
