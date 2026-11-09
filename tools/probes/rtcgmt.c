/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * Prints whether the kernel takes the hardware clock as GMT or local time
 * (set at boot from ~/config/settings/RTC_time_settings by the launch
 * daemon) and the time zone offset it has.
 *
 * Build: $CC -O2 -o rtcgmt rtcgmt.c
 */
#include <stdbool.h>
#include <stdio.h>

#include <OS.h>


/* libroot's syscalls (headers/private/system/syscalls.h) */
extern status_t _kern_get_real_time_clock_is_gmt(bool* _isGMT);
extern status_t _kern_get_timezone(int32* _timezoneOffset, char* name,
	size_t nameLength);


int
main(void)
{
	bool isGMT = false;
	int32 offset = 0;
	char name[64] = "";
	_kern_get_real_time_clock_is_gmt(&isGMT);
	_kern_get_timezone(&offset, name, sizeof(name));
	printf("hardware clock: %s; time zone %s, %+d s from GMT\n",
		isGMT ? "GMT" : "local time", name, (int)offset);
	return 0;
}
