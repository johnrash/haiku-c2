/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * wifimerge: checks WirelessNetworkMenuItem::MergeNetworks() (the Deskbar
 * network menu's and the Network preferences' list of wireless networks,
 * milestone M77) on made-up scan results: one entry per network name, the
 * associated access point or else the strongest, hidden networks left out.
 * Linked with the build's WirelessNetworkMenuItem.o and RadioView.o:
 *
 * $CXX -O2 $HAIKU_CPPFLAGS -I<haiku>/src/apps/networkstatus -o wifimerge
 *     wifimerge.cpp <objects>/apps/networkstatus/WirelessNetworkMenuItem.o
 *     <objects>/apps/networkstatus/RadioView.o -lbe -lbnetapi
 *     <objects>/kits/locale/liblocalestub.a
 */


#include <stdio.h>
#include <string.h>

#include "WirelessNetworkMenuItem.h"


static int sFailures = 0;


static wireless_network
make_network(const char* name, uint8 last, uint8 signal)
{
	wireless_network network;
	memset(&network, 0, sizeof(network));
	strncpy(network.name, name, sizeof(network.name));
	uint8 mac[6] = { 0x02, 0x04, 0x0a, 0x10, 0xab, last };
	network.address.SetToLinkLevel(mac, sizeof(mac));
	network.signal_strength = signal;
	return network;
}


static void
check(bool condition, const char* what)
{
	printf("%s: %s\n", condition ? "ok" : "FAILED", what);
	if (!condition)
		sFailures++;
}


static int32
find(const wireless_network* networks, uint32 count, const char* name)
{
	for (uint32 i = 0; i < count; i++) {
		if (strncmp(networks[i].name, name, sizeof(networks[i].name)) == 0)
			return i;
	}
	return -1;
}


int
main()
{
	// "Home" through three access points or bands, two hidden networks,
	// "Guest", and a name of the full 32 bytes (no terminating null), twice
	const char* longName = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";
	wireless_network scan[] = {
		make_network("Home", 1, 40),
		make_network("", 2, 90),
		make_network("Home", 3, 70),
		make_network("Guest", 4, 30),
		make_network("Home", 5, 55),
		make_network("", 6, 20),
		make_network(longName, 7, 10),
		make_network(longName, 8, 60),
	};
	const uint32 scanCount = sizeof(scan) / sizeof(scan[0]);

	// not associated: the strongest of each
	wireless_network networks[scanCount];
	memcpy(networks, scan, sizeof(scan));
	std::set<BNetworkAddress> none;
	uint32 count = WirelessNetworkMenuItem::MergeNetworks(networks, scanCount,
		none);
	check(count == 3, "3 networks of 8 results (Home, Guest, the long name)");
	int32 home = find(networks, count, "Home");
	check(home >= 0 && networks[home].signal_strength == 70,
		"Home: the strongest access point (70)");
	check(home >= 0 && networks[home].address == scan[2].address,
		"Home: the strongest access point's address");
	int32 guest = find(networks, count, "Guest");
	check(guest >= 0 && networks[guest].signal_strength == 30, "Guest kept");
	int32 longIndex = find(networks, count, longName);
	check(longIndex >= 0 && networks[longIndex].signal_strength == 60,
		"the 32-byte name: the strongest (60)");
	check(find(networks, count, "") < 0, "no hidden networks");

	// associated with Home's weakest access point: that one stays
	memcpy(networks, scan, sizeof(scan));
	std::set<BNetworkAddress> associated;
	associated.insert(scan[0].address);
	count = WirelessNetworkMenuItem::MergeNetworks(networks, scanCount,
		associated);
	home = find(networks, count, "Home");
	check(count == 3, "associated: still 3 networks");
	check(home >= 0 && networks[home].address == scan[0].address,
		"associated: Home is the associated access point (40)");

	// nothing but hidden networks
	wireless_network hidden[] = {
		make_network("", 1, 50),
		make_network("", 2, 60),
	};
	count = WirelessNetworkMenuItem::MergeNetworks(hidden, 2, none);
	check(count == 0, "only hidden networks: none listed");

	// one network, nothing to merge
	wireless_network single[] = { make_network("Solo", 9, 33) };
	count = WirelessNetworkMenuItem::MergeNetworks(single, 1, none);
	check(count == 1 && single[0].signal_strength == 33, "a single network");

	printf("%s\n", sFailures == 0 ? "wifimerge: all passed"
		: "wifimerge: failures");
	return sFailures == 0 ? 0 : 1;
}
