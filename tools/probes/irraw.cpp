/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * irraw: what the infrared receiver (meson_ir) gets, for learning remotes.
 *   irraw pulses <seconds>   the raw marks and spaces, one "m <us>" or
 *                            "s <us>" per line (tools/tests/irdecoder reads
 *                            them back); a line "s 100000" after a pause
 *   irraw events <seconds>   the decoded presses, and the counters
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>

#include <ir_remote.h>


static const char* kDevice = "/dev/input/remote/meson_ir/0";


static const char*
ProtocolName(uint32 protocol)
{
	switch (protocol) {
		case IR_REMOTE_NEC: return "NEC";
		case IR_REMOTE_NEC_EXTENDED: return "NEC extended";
		case IR_REMOTE_NEC32: return "NEC32";
		case IR_REMOTE_RC5: return "RC-5";
		case IR_REMOTE_RC5X: return "RC-5X";
		case IR_REMOTE_RC6: return "RC-6";
		case IR_REMOTE_SONY12: return "Sony 12";
		case IR_REMOTE_SONY15: return "Sony 15";
		case IR_REMOTE_SONY20: return "Sony 20";
		case IR_REMOTE_JVC: return "JVC";
	}
	return "?";
}


static int
Pulses(int fd, bigtime_t end)
{
	bool collect = true;
	if (ioctl(fd, IR_REMOTE_COLLECT_PULSES, &collect, sizeof(collect)) != 0) {
		fprintf(stderr, "irraw: collecting pulses: %s\n", strerror(errno));
		return 1;
	}
	bigtime_t lastPulse = system_time();
	bool paused = true;
	while (system_time() < end) {
		ir_remote_pulse pulses[256];
		int size = ioctl(fd, IR_REMOTE_READ_PULSES, pulses, sizeof(pulses));
		if (size < 0) {
			fprintf(stderr, "irraw: reading pulses: %s\n", strerror(errno));
			return 1;
		}
		int count = size / (int)sizeof(ir_remote_pulse);
		for (int i = 0; i < count; i++) {
			printf("%c %u\n", pulses[i].mark ? 'm' : 's',
				(unsigned)pulses[i].duration);
		}
		if (count > 0) {
			lastPulse = system_time();
			paused = false;
			fflush(stdout);
		} else if (!paused && system_time() - lastPulse > 100000) {
			// the space after the last mark has no edge yet
			printf("s 100000\n");
			fflush(stdout);
			paused = true;
		}
		snooze(20000);
	}
	collect = false;
	ioctl(fd, IR_REMOTE_COLLECT_PULSES, &collect, sizeof(collect));
	return 0;
}


static int
Events(int fd, bigtime_t end)
{
	while (system_time() < end) {
		ir_remote_event events[16];
		ssize_t size = read(fd, events, sizeof(events));
		if (size < 0 && errno != B_WOULD_BLOCK && errno != EAGAIN) {
			fprintf(stderr, "irraw: read: %s\n", strerror(errno));
			return 1;
		}
		for (ssize_t i = 0; i < size / (ssize_t)sizeof(ir_remote_event);
				i++) {
			const ir_remote_event& e = events[i];
			printf("%s address %#" B_PRIx32 " command %#" B_PRIx32 "%s%s\n",
				ProtocolName(e.protocol), e.address, e.command,
				(e.flags & IR_REMOTE_REPEAT) != 0 ? " repeat" : "",
				(e.flags & IR_REMOTE_TOGGLE) != 0 ? " toggle" : "");
			fflush(stdout);
		}
		snooze(20000);
	}
	ir_remote_stats stats;
	if (ioctl(fd, IR_REMOTE_GET_STATS, &stats, sizeof(stats)) == 0) {
		printf("frames %" B_PRIu32 " (repeats %" B_PRIu32 "), unknown "
			"trains %" B_PRIu32 ", edges %" B_PRIu32 ", overruns %" B_PRIu32
			"\n", stats.frames, stats.repeats, stats.unknown, stats.edges,
			stats.overruns);
	}
	return 0;
}


int
main(int argc, char** argv)
{
	if (argc < 3 || (strcmp(argv[1], "pulses") != 0
			&& strcmp(argv[1], "events") != 0)) {
		fprintf(stderr, "usage: irraw pulses|events <seconds>\n");
		return 2;
	}
	bigtime_t end = system_time() + (bigtime_t)(atof(argv[2]) * 1000000);
	// non-blocking from the open: the driver keeps the mode it was opened with
	int fd = open(kDevice, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "irraw: %s: %s\n", kDevice, strerror(errno));
		return 1;
	}
	int result = strcmp(argv[1], "pulses") == 0 ? Pulses(fd, end)
		: Events(fd, end);
	close(fd);
	return result;
}
