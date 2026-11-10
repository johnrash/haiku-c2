/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * usbpower: each USB device's power, as its descriptors and status say:
 * the current its active configuration asks for (bMaxPower), whether it
 * says it is self-powered (the configuration's attributes, and the
 * device's GET_STATUS), for working out a hub's budget (milestone M72).
 *
 * Build: $CXX -O2 -o usbpower usbpower.cpp -ldevice -lbe
 */


#include <stdio.h>
#include <string.h>

#include <OS.h>
#include <USBKit.h>


class Lister : public BUSBRoster {
public:
	virtual status_t DeviceAdded(BUSBDevice* device)
	{
		const BUSBConfiguration* configuration
			= device->ActiveConfiguration();
		uint32 maxPower = 0;
		bool configSelf = false;
		if (configuration != NULL) {
			const usb_configuration_descriptor* descriptor
				= configuration->Descriptor();
			maxPower = descriptor->max_power * 2;
			configSelf = (descriptor->attributes & 0x40) != 0;
		}
		uint16 status = 0;
		ssize_t length = device->ControlTransfer(0x80, 0x00, 0, 0,
			sizeof(status), &status);
		printf("%04x:%04x %-28s %-32s %4u mA  config: %s  status: %s\n",
			device->VendorID(), device->ProductID(), device->Location(),
			device->ProductString(), (unsigned)maxPower,
			configSelf ? "self-powered" : "bus-powered",
			length == sizeof(status) ? ((status & 1) ? "self-powered"
				: "bus-powered") : "?");
		return B_OK;
	}

	virtual void DeviceRemoved(BUSBDevice* device)
	{
	}
};


int
main()
{
	Lister lister;
	lister.Start();
	snooze(500000);
	lister.Stop();
	return 0;
}
