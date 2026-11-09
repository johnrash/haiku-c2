/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * usbport: a USB hub's ports through its class requests (USB 2.0, 11.24),
 * to unplug and plug a device without hands.
 *   usbport <hub, e.g. /dev/bus/usb/0/0/hub> status
 *       the hub descriptor's port count and power switching, and each
 *       port's status (connected, enabled, powered, high/low speed)
 *   usbport <hub> <port> off|on|cycle [<ms off>]
 *       port power off, on, or off for <ms> (default 2000) and on again
 *   usbport <hub> <port> disable|reset
 *       the port disabled (CLEAR_FEATURE PORT_ENABLE), or reset
 *       (SET_FEATURE PORT_RESET)
 * Ports are numbered from 1, as the hub numbers them (Haiku's device paths
 * count from 0: /dev/bus/usb/0/0/2 is the hub's port 3).
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <OS.h>
#include <USBKit.h>


static const uint8 kGetStatus = 0;
static const uint8 kClearFeature = 1;
static const uint8 kSetFeature = 3;
static const uint8 kGetDescriptor = 6;
static const uint16 kPortEnable = 1;
static const uint16 kPortReset = 4;
static const uint16 kPortPower = 8;


// usb_raw refuses a NULL data pointer even for a request without data
static uint8 sNoData[4];


static bool
port_status(BUSBDevice& hub, int port, uint16* status)
{
	uint16 words[2];
	ssize_t result = hub.ControlTransfer(0xa3, kGetStatus, 0, port,
		sizeof(words), words);
	if (result != (ssize_t)sizeof(words))
		return false;
	*status = words[0];
	return true;
}


static void
print_status(BUSBDevice& hub, int port)
{
	uint16 status;
	if (!port_status(hub, port, &status)) {
		printf("port %d: GET_STATUS failed\n", port);
		return;
	}
	printf("port %d: %#06x%s%s%s%s%s\n", port, status,
		status & 0x0001 ? " connected" : "",
		status & 0x0002 ? " enabled" : "",
		status & 0x0100 ? " powered" : " NOT powered",
		status & 0x0200 ? " low-speed" : "",
		status & 0x0400 ? " high-speed" : "");
}


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: usbport <hub> status\n"
			"       usbport <hub> <port> off|on|cycle [<ms off>]\n");
		return 1;
	}
	BUSBDevice hub(argv[1]);
	if (hub.InitCheck() != B_OK) {
		fprintf(stderr, "usbport: %s: %s\n", argv[1],
			strerror(hub.InitCheck()));
		return 1;
	}

	// the hub descriptor: bNbrPorts, wHubCharacteristics
	uint8 descriptor[9];
	ssize_t length = hub.ControlTransfer(0xa0, kGetDescriptor, 0x2900, 0,
		sizeof(descriptor), descriptor);
	if (length < 5) {
		fprintf(stderr, "usbport: no hub descriptor (%zd)\n", length);
		return 1;
	}
	int ports = descriptor[2];
	int switching = descriptor[3] & 0x3;

	if (strcmp(argv[2], "status") == 0) {
		printf("%s: %s \"%s\", %d ports, power switching %s\n", argv[1],
			hub.ManufacturerString(), hub.ProductString(), ports,
			switching == 0 ? "ganged" : switching == 1 ? "per port" : "none");
		for (int port = 1; port <= ports; port++)
			print_status(hub, port);
		return 0;
	}

	int port = atoi(argv[2]);
	if (port < 1 || port > ports || argc < 4) {
		fprintf(stderr, "usbport: port 1 to %d, and off, on or cycle\n",
			ports);
		return 1;
	}
	const char* action = argv[3];
	bigtime_t off = argc > 4 ? atoi(argv[4]) * 1000LL : 2000000;

	if (strcmp(action, "off") == 0 || strcmp(action, "cycle") == 0) {
		ssize_t result = hub.ControlTransfer(0x23, kClearFeature, kPortPower,
			port, 0, sNoData);
		printf("port %d power off: %s\n", port,
			result >= 0 ? "done" : strerror(result));
		snooze(100000);
		print_status(hub, port);
		if (strcmp(action, "off") == 0)
			return result >= 0 ? 0 : 1;
		snooze(off > 100000 ? off - 100000 : 0);
	}
	if (strcmp(action, "on") == 0 || strcmp(action, "cycle") == 0) {
		ssize_t result = hub.ControlTransfer(0x23, kSetFeature, kPortPower,
			port, 0, sNoData);
		printf("port %d power on: %s\n", port,
			result >= 0 ? "done" : strerror(result));
		snooze(500000);
		print_status(hub, port);
		return result >= 0 ? 0 : 1;
	}
	if (strcmp(action, "disable") == 0 || strcmp(action, "reset") == 0) {
		bool reset = strcmp(action, "reset") == 0;
		ssize_t result = hub.ControlTransfer(0x23,
			reset ? kSetFeature : kClearFeature,
			reset ? kPortReset : kPortEnable, port, 0, sNoData);
		printf("port %d %s: %s\n", port, action,
			result >= 0 ? "done" : strerror(result));
		snooze(500000);
		print_status(hub, port);
		return result >= 0 ? 0 : 1;
	}
	fprintf(stderr, "usbport: off, on, cycle, disable or reset\n");
	return 1;
}
