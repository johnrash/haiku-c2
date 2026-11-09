/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * usbhub <hub device, e.g. /dev/bus/usb/0/0/hub> status|power-cycle|
 * clear-tt|reset-tt <port, 1-based>: a hub's port status, or a power cycle
 * of the port (off for a second), or the hub's transaction translator
 * buffers cleared or reset for it (USB 2.0 11.24.2: CLEAR_TT_BUFFER for
 * control endpoint 0 of every address, RESET_TT).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <OS.h>
#include <USBKit.h>


static const uint8 kPortPower = 8;
static const uint8 kClearTTBuffer = 8;
static const uint8 kResetTT = 9;


static void
print_status(BUSBDevice& hub, int port)
{
	uint16 status[2];
	ssize_t result = hub.ControlTransfer(USB_REQTYPE_CLASS
		| USB_REQTYPE_OTHER_IN, USB_REQUEST_GET_STATUS, 0, port, 4, status);
	if (result != 4)
		printf("port %d: status failed: %s\n", port, strerror(result));
	else {
		printf("port %d: status %#06x change %#06x\n", port, status[0],
			status[1]);
	}
}


int
main(int argc, char** argv)
{
	if (argc < 4) {
		printf("usage: usbhub <hub> status|power-cycle|clear-tt|reset-tt"
			" <port>\n");
		return 1;
	}
	BUSBDevice hub(argv[1]);
	if (hub.InitCheck() != B_OK) {
		printf("usbhub: %s: %s\n", argv[1], strerror(hub.InitCheck()));
		return 1;
	}
	int port = atoi(argv[3]);

	uint8 descriptor[16];
	ssize_t length = hub.ControlTransfer(USB_REQTYPE_CLASS
		| USB_REQTYPE_DEVICE_IN, USB_REQUEST_GET_DESCRIPTOR, 0x29 << 8, 0,
		sizeof(descriptor), descriptor);
	if (length >= 5) {
		printf("hub: %d ports, characteristics %#06x\n", descriptor[2],
			descriptor[3] | (descriptor[4] << 8));
	}
	print_status(hub, port);

	ssize_t result = 0;
	if (strcmp(argv[2], "power-cycle") == 0) {
		result = hub.ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
			USB_REQUEST_CLEAR_FEATURE, kPortPower, port, 0, NULL);
		print_status(hub, port);
		snooze(1000000);
		if (result >= 0) {
			result = hub.ControlTransfer(USB_REQTYPE_CLASS
				| USB_REQTYPE_OTHER_OUT, USB_REQUEST_SET_FEATURE, kPortPower,
				port, 0, NULL);
		}
		snooze(500000);
		print_status(hub, port);
	} else if (strcmp(argv[2], "clear-tt") == 0) {
		// control endpoint 0 of each address, both directions
		for (int address = 1; address < 128 && result >= 0; address++) {
			for (int in = 0; in < 2; in++) {
				uint16 value = (in << 15) | (address << 4);
				result = hub.ControlTransfer(USB_REQTYPE_CLASS
					| USB_REQTYPE_OTHER_OUT, kClearTTBuffer, value, port, 0,
					NULL);
			}
		}
	} else if (strcmp(argv[2], "reset-tt") == 0) {
		result = hub.ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
			kResetTT, 0, port, 0, NULL);
	}
	if (result < 0)
		printf("usbhub: %s: %s\n", argv[2], strerror(result));
	return result < 0 ? 1 : 0;
}
