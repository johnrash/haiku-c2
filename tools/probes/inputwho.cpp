/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 *   inputwho
 *
 * Which input device keeps a board busy with events? Lists the devices and any
 * keys held down, then stops each device for a few seconds and watches
 * idle_time(), the time since app_server last saw input. Every device is
 * started again after its turn (milestone M79).
 *
 * Build: $CXX -O2 $HAIKU_CXX_INCLUDES -o inputwho inputwho.cpp -lbe
 */


#include <stdio.h>

#include <Application.h>
#include <Input.h>
#include <InterfaceDefs.h>
#include <List.h>
#include <OS.h>


static const char*
type_name(input_device_type type)
{
	switch (type) {
		case B_POINTING_DEVICE:
			return "pointing";
		case B_KEYBOARD_DEVICE:
			return "keyboard";
		default:
			return "other";
	}
}


/*!	The longest idle_time() seen in \a seconds. */
static double
watch(int seconds)
{
	bigtime_t longest = 0;
	for (int i = 0; i < seconds * 10; i++) {
		snooze(100000);
		bigtime_t idle = idle_time();
		if (idle > longest)
			longest = idle;
	}
	return longest / 1e6;
}


int
main()
{
	BApplication app("application/x-vnd.c2-inputwho");

	key_info keys;
	if (get_key_info(&keys) == B_OK) {
		printf("modifiers 0x%" B_PRIx32 ", keys down:", keys.modifiers);
		int down = 0;
		for (int i = 0; i < 128; i++) {
			if ((keys.key_states[i / 8] & (1 << (7 - i % 8))) != 0) {
				printf(" 0x%02x", i);
				down++;
			}
		}
		printf(down == 0 ? " none\n" : "\n");
	}

	printf("all devices on: longest idle in 6 s: %.1f s\n", watch(6));

	BList devices;
	if (get_input_devices(&devices) != B_OK) {
		printf("no input devices\n");
		return 1;
	}
	for (int32 i = 0; i < devices.CountItems(); i++) {
		BInputDevice* device = (BInputDevice*)devices.ItemAt(i);
		bool running = device->IsRunning();
		printf("%-40s %-8s %s", device->Name(), type_name(device->Type()),
			running ? "running" : "stopped");
		fflush(stdout);
		if (running) {
			device->Stop();
			double longest = watch(5);
			device->Start();
			printf(": without it, longest idle %.1f s", longest);
		}
		printf("\n");
		fflush(stdout);
		snooze(500000);
	}
	return 0;
}
