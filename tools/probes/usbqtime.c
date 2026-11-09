/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * usbqtime [seconds [rounds]]: prints, every interval, the counters a
 * temporary realtekwifi test build keeps in the kernel area
 * "usb_test_stats" (USB callback delays and the send queue; milestone M82 (docs/milestones.md)).
 * It resets the maxima after reading them.
 */
#include <OS.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void
direction(const char* name, const int64_t* c, const int64_t* d, double secs)
{
	int64_t n = d[0] > 0 ? d[0] : 1;
	printf(" %s %5.0f/s wait %4lld max %5lld [%lld %lld %lld %lld %lld]"
		" run %4lld max %5lld bus %5lld max %6lld %5.2f MB/s err %lld",
		name, d[0] / secs, (long long)(d[1] / n), (long long)c[2],
		(long long)d[8], (long long)d[9], (long long)d[10], (long long)d[11],
		(long long)d[12], (long long)(d[4] / n), (long long)c[5],
		(long long)(d[6] / n), (long long)c[7], d[13] / secs / 1e6,
		(long long)d[14]);
}

int
main(int argc, char** argv)
{
	int interval = argc > 1 ? atoi(argv[1]) : 10;
	int rounds = argc > 2 ? atoi(argv[2]) : 1000000;
	area_id source = find_area("usb_test_stats");
	if (source < 0) {
		fprintf(stderr, "no area usb_test_stats\n");
		return 1;
	}
	volatile int64_t* s;
	area_id area = clone_area("usb_test_stats reader", (void**)&s,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, source);
	if (area < 0) {
		fprintf(stderr, "clone_area: %s\n", strerror(area));
		return 1;
	}
	static const int kMaxima[] = { 2, 5, 7, 18, 21, 23, 36, 45, 49 };
	int64_t prev[64], cur[64], d[64];
	for (int i = 0; i < 64; i++)
		prev[i] = s[i];
	for (unsigned i = 0; i < sizeof(kMaxima) / sizeof(kMaxima[0]); i++)
		s[kMaxima[i]] = 0;
	bigtime_t last = system_time();
	for (int r = 0; r < rounds; r++) {
		snooze((bigtime_t)interval * 1000000);
		for (int i = 0; i < 64; i++)
			cur[i] = s[i];
		for (unsigned i = 0; i < sizeof(kMaxima) / sizeof(kMaxima[0]); i++)
			s[kMaxima[i]] = 0;
		bigtime_t now = system_time();
		double secs = (now - last) / 1e6;
		last = now;
		for (int i = 0; i < 64; i++)
			d[i] = cur[i] - prev[i];
		memcpy(prev, cur, sizeof(prev));

		char stamp[16];
		time_t t = time(NULL);
		strftime(stamp, sizeof(stamp), "%H:%M:%S", localtime(&t));
		printf("%s", stamp);
		direction("in", cur, d, secs);
		int64_t batches = d[42] > 0 ? d[42] : 1;
		printf(" frames/batch %.1f max %lld deliver %lld max %lld\n",
			(double)d[41] / batches, (long long)cur[49],
			(long long)(d[44] / batches), (long long)cur[45]);
		printf("%s", stamp);
		direction("out", cur + 16, d + 16, secs);
		int64_t xfers = d[40] > 0 ? d[40] : 1;
		printf(" frames/xfer %.2f | queued %.0f/s acks %.0f/s tcp %.0f/s"
			" dropped acks %lld tcp %lld other %lld max %lld"
			" slots full %.1f%% (%lld) blocked %lld\n",
			(double)d[39] / xfers, d[32] / secs, d[43] / secs, d[48] / secs,
			(long long)d[33], (long long)d[34], (long long)d[35],
			(long long)cur[36], d[37] / (secs * 1e4), (long long)d[38],
			(long long)d[47]);
		fflush(stdout);
	}
	return 0;
}
