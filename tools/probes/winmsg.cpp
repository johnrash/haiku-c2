/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * winmsg <app signature> <window index> <what>: sends a message with a
 * numeric code (hex with 0x) to one of an application's windows, as a menu
 * shortcut would; for driving an application over SSH, e.g. MediaPlayer's
 * full screen toggle: winmsg application/x-vnd.Haiku-MediaPlayer 0 0x1008
 */
#include <stdio.h>
#include <stdlib.h>

#include <Message.h>
#include <Messenger.h>


int
main(int argc, char** argv)
{
	if (argc != 4) {
		fprintf(stderr, "usage: winmsg <app signature> <window index> "
			"<what>\n");
		return 1;
	}
	BMessenger app(argv[1]);
	if (!app.IsValid()) {
		fprintf(stderr, "winmsg: %s is not running\n", argv[1]);
		return 1;
	}
	BMessage message((uint32)strtoul(argv[3], NULL, 0));
	message.AddSpecifier("Window", (int32)atoi(argv[2]));
	status_t status = app.SendMessage(&message);
	if (status != B_OK) {
		fprintf(stderr, "winmsg: %s\n", strerror(status));
		return 1;
	}
	return 0;
}
