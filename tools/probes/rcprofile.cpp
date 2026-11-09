/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * rcprofile <name> <protocol> <address> <application|-> <command>:<key>...
 * writes a Remote Control profile (~/config/settings/Remote Control/) with
 * keystroke buttons (raw key codes), for the application by signature ("-":
 * any); rcprofile -r <name> removes it. Then "rcpress reload". Build with
 * the add-on's RemoteProfile.cpp (-I its directory and headers/private/
 * drivers, -lbe).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "RemoteProfile.h"


using RemoteControl::Button;
using RemoteControl::Profile;


int
main(int argc, char** argv)
{
	if (argc == 3 && strcmp(argv[1], "-r") == 0) {
		Profile profile;
		profile.SetName(argv[2]);
		return profile.Remove() == B_OK ? 0 : 1;
	}
	if (argc < 6) {
		fprintf(stderr, "usage: rcprofile <name> <protocol> <address> "
			"<application|-> <command>:<key>...\n"
			"       rcprofile -r <name>\n");
		return 1;
	}
	Profile profile;
	profile.SetName(argv[1]);
	profile.SetRemote(strtoul(argv[2], NULL, 0), strtoul(argv[3], NULL, 0));
	if (strcmp(argv[4], "-") != 0)
		profile.SetApplication(argv[4]);
	for (int i = 5; i < argc; i++) {
		char* key = strchr(argv[i], ':');
		if (key == NULL)
			return 1;
		Button button;
		button.label = argv[i];
		button.command = strtoul(argv[i], NULL, 0);
		button.key = strtoul(key + 1, NULL, 0);
		button.modifiers = 0;
		button.repeat = false;
		profile.AddButton(button);
	}
	status_t status = profile.Save();
	if (status != B_OK)
		fprintf(stderr, "rcprofile: %s\n", strerror(status));
	return status == B_OK ? 0 : 1;
}
