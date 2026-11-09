/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * Sets or clears IFF_CONFIGURING on an interface, as net_server's DHCP
 * client does while it waits for a lease; the address stays as it is.
 *
 *   ifflags <interface> +configuring|-configuring|show
 *
 * Build: g++ -o ifflags ifflags.cpp -lbnetapi
 */


#include <stdio.h>
#include <string.h>

#include <net/if.h>

#include <NetworkInterface.h>


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s <interface> +configuring|-configuring|"
			"show\n", argv[0]);
		return 1;
	}

	BNetworkInterface interface(argv[1]);
	if (!interface.Exists()) {
		fprintf(stderr, "ifflags: no interface %s\n", argv[1]);
		return 1;
	}

	uint32 flags = interface.Flags();
	if (strcmp(argv[2], "+configuring") == 0)
		flags |= IFF_CONFIGURING;
	else if (strcmp(argv[2], "-configuring") == 0)
		flags &= ~IFF_CONFIGURING;
	else if (strcmp(argv[2], "show") != 0) {
		fprintf(stderr, "ifflags: unknown command %s\n", argv[2]);
		return 1;
	}

	if (flags != interface.Flags()) {
		status_t status = interface.SetFlags(flags);
		if (status != B_OK) {
			fprintf(stderr, "ifflags: %s\n", strerror(status));
			return 1;
		}
	}

	printf("%s flags 0x%" B_PRIx32 "%s\n", argv[1], interface.Flags(),
		(interface.Flags() & IFF_CONFIGURING) != 0 ? " (configuring)" : "");
	return 0;
}
