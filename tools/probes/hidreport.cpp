/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 *   hidreport
 *
 * For each USB HID interface: its report descriptor, and what GET_REPORT says
 * the input report is now for report IDs 0 to 7, repeated three times. Tells a
 * button a device really reports as held from a release that got lost
 * (milestone M79 (docs/milestones.md)).
 *
 * Build: $CXX -O2 $HAIKU_CXX_INCLUDES -o hidreport hidreport.cpp -ldevice -lbe
 */


#include <stdio.h>

#include <OS.h>
#include <USBKit.h>


static void
print_bytes(const uint8* data, ssize_t length)
{
	for (ssize_t i = 0; i < length; i++)
		printf("%02x ", data[i]);
	printf("\n");
}


class Lister : public BUSBRoster {
public:
	virtual status_t DeviceAdded(BUSBDevice* device)
	{
		const BUSBConfiguration* configuration
			= device->ActiveConfiguration();
		if (configuration == NULL)
			return B_OK;
		for (uint32 i = 0; i < configuration->CountInterfaces(); i++) {
			const BUSBInterface* interface = configuration->InterfaceAt(i);
			if (interface == NULL || interface->Class() != 3)
				continue;
			printf("%04x:%04x %s, interface %" B_PRIu32 " (%s)\n",
				device->VendorID(), device->ProductID(),
				device->ProductString(), interface->Index(),
				device->Location());

			uint8 descriptor[1024];
			ssize_t length = device->ControlTransfer(0x81, 0x06, 0x2200,
				interface->Index(), sizeof(descriptor), descriptor);
			if (length > 0) {
				printf("  report descriptor (%zd bytes): ", length);
				print_bytes(descriptor, length);
			} else
				printf("  no report descriptor: %zd\n", length);

			for (int round = 0; round < 3; round++) {
				for (int id = 0; id < 8; id++) {
					uint8 report[64];
					ssize_t size = device->ControlTransfer(0xa1, 0x01,
						0x0100 | id, interface->Index(), sizeof(report),
						report);
					if (size > 0) {
						printf("  round %d, report id %d: ", round, id);
						print_bytes(report, size);
					}
				}
				snooze(300000);
			}
		}
		fflush(stdout);
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
	snooze(8000000);
	lister.Stop();
	return 0;
}
