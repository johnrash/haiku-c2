/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * clocktest power|latency : sets the scheduler mode (as ProcessController does)
 * clocktest busy <n>      : times n fixed loops of work (CPU speed under load)
 * clocktest burst <n>     : n times: idle 2 s, then time 20 M iterations of work
 */
#include <OS.h>
#include <scheduler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile unsigned long sSink;

static bigtime_t
work(unsigned long iterations)
{
	bigtime_t start = system_time();
	unsigned long x = 1;
	for (unsigned long i = 0; i < iterations; i++)
		x = x * 2862933555777941757UL + 3037000493UL;
	sSink = x;
	return system_time() - start;
}

int
main(int argc, char** argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc >= 2 && strcmp(argv[1], "power") == 0)
		return set_scheduler_mode(SCHEDULER_MODE_POWER_SAVING) == B_OK ? 0 : 1;
	if (argc >= 2 && strcmp(argv[1], "latency") == 0)
		return set_scheduler_mode(SCHEDULER_MODE_LOW_LATENCY) == B_OK ? 0 : 1;
	if (argc >= 3 && strcmp(argv[1], "busy") == 0) {
		int n = atoi(argv[2]);
		bigtime_t total = 0;
		for (int i = 0; i < n; i++)
			total += work(200000000UL);
		printf("busy: %d x 200M iterations in %.3f s\n", n, total / 1e6);
		return 0;
	}
	if (argc >= 3 && strcmp(argv[1], "burst") == 0) {
		int n = atoi(argv[2]);
		for (int i = 0; i < n; i++) {
			snooze(2000000);
			printf("burst %d: %.1f ms\n", i, work(20000000UL) / 1000.0);
		}
		return 0;
	}
	fprintf(stderr, "usage: clocktest power|latency|busy <n>|burst <n>\n");
	return 2;
}
