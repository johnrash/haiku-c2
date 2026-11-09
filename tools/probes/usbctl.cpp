/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * usbctl <device, e.g. /dev/bus/usb/0/0/1> [<count>]: how reliably control
 * transfers reach a USB device: <count> times (default 20) each of
 * GET_DESCRIPTOR device, string 0 (languages), string 2 and GET_STATUS,
 * with the results per request (bytes, or the error).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <USBKit.h>


struct request {
	const char*	name;
	uint8		type;
	uint8		request;
	uint16		value;
	uint16		index;
	uint16		length;
};


int
main(int argc, char** argv)
{
	if (argc < 2) {
		printf("usage: usbctl <device> [<count>]\n");
		return 1;
	}
	int count = argc > 2 ? atoi(argv[2]) : 20;
	BUSBDevice device(argv[1]);
	if (device.InitCheck() != B_OK) {
		printf("usbctl: %s: %s\n", argv[1], strerror(device.InitCheck()));
		return 1;
	}

	const request requests[] = {
		{ "device descriptor", USB_REQTYPE_DEVICE_IN,
			USB_REQUEST_GET_DESCRIPTOR, USB_DESCRIPTOR_DEVICE << 8, 0, 18 },
		{ "string 0", USB_REQTYPE_DEVICE_IN, USB_REQUEST_GET_DESCRIPTOR,
			USB_DESCRIPTOR_STRING << 8, 0, 255 },
		{ "string 2", USB_REQTYPE_DEVICE_IN, USB_REQUEST_GET_DESCRIPTOR,
			(USB_DESCRIPTOR_STRING << 8) | 2, 0x0409, 255 },
		{ "status", USB_REQTYPE_DEVICE_IN, USB_REQUEST_GET_STATUS, 0, 0, 2 },
	};
	for (const request& r : requests) {
		int ok = 0;
		ssize_t lastError = 0, lastLength = 0;
		int errors = 0;
		for (int i = 0; i < count; i++) {
			uint8 buffer[256];
			ssize_t result = device.ControlTransfer(r.type, r.request,
				r.value, r.index, r.length, buffer);
			if (result >= 0) {
				ok++;
				lastLength = result;
			} else {
				errors++;
				lastError = result;
			}
		}
		printf("%-18s %d ok (%zd bytes), %d failed%s%s\n", r.name, ok,
			lastLength, errors, errors > 0 ? ": " : "",
			errors > 0 ? strerror(lastError) : "");
	}
	return 0;
}
