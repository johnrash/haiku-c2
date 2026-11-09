/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 *   idletime [seconds]
 *
 * Prints idle_time(), the time since app_server last saw input, each second,
 * and whether screen_blanker runs: is something keeping a board from its
 * screen saver with input events (milestone M79 (docs/milestones.md))?
 *
 * Build: $CXX -O2 $HAIKU_CPPFLAGS -o idletime idletime.cpp -lbe
 */


#include <stdio.h>
#include <stdlib.h>

#include <Application.h>
#include <InterfaceDefs.h>
#include <OS.h>
#include <Roster.h>


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 60;
	BApplication app("application/x-vnd.c2-idletime");
	int resets = 0;
	bigtime_t last = idle_time();
	for (int i = 0; i < seconds; i++) {
		snooze(1000000);
		bigtime_t idle = idle_time();
		if (idle < last)
			resets++;
		last = idle;
		printf("%3d s: idle %6.1f s, %d resets, screen_blanker %s\n", i + 1,
			idle / 1e6, resets,
			be_roster->IsRunning("application/x-vnd.Haiku.screenblanker")
				? "running" : "not running");
		fflush(stdout);
	}
	return 0;
}
