/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * rcpress <protocol> <address> <command>: a press of a remote's button, as
 * the Remote Control preferences' Test button makes one (Control() code
 * 'rcTB' of the "Remote Control" input device), for tests without the
 * remote. Numbers in C notation: rcpress 10 0x8f 0x8e is the JVC RM-RK52's
 * ATT (protocol 10 is JVC in ir_remote.h).
 * rcpress reload: the add-on reads the profile files again ('rcRL').
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Input.h>
#include <Message.h>


int
main(int argc, char** argv)
{
	bool reload = argc == 2 && strcmp(argv[1], "reload") == 0;
	if (argc != 4 && !reload) {
		fprintf(stderr, "usage: rcpress <protocol> <address> <command>\n"
			"       rcpress reload\n");
		return 1;
	}
	BInputDevice* device = find_input_device("Remote Control");
	if (device == NULL) {
		fprintf(stderr, "rcpress: no \"Remote Control\" input device\n");
		return 1;
	}
	if (reload) {
		BMessage empty;
		status_t status = device->Control('rcRL', &empty);
		delete device;
		return status == B_OK ? 0 : 1;
	}
	BMessage press;
	press.AddInt32("protocol", (int32)strtoul(argv[1], NULL, 0));
	press.AddInt32("address", (int32)strtoul(argv[2], NULL, 0));
	press.AddInt32("command", (int32)strtoul(argv[3], NULL, 0));
	status_t status = device->Control('rcTB', &press);
	delete device;
	if (status != B_OK) {
		fprintf(stderr, "rcpress: %s\n", strerror(status));
		return 1;
	}
	return 0;
}
