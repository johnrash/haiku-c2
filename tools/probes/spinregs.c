/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * Busy threads with known register contents, for the kernel debugger's
 * "bt <thread>" and "iframe -f <thread>" on threads running on other CPUs:
 * thread n holds 0x5a5a0000000000nn in x19 and v31 = (0x1111111111111111 * n,
 * 0xfedcba98765432nn) while it spins, for <seconds> seconds.
 *
 *   spinregs <threads> <seconds>
 *
 * Build: $CC -O2 -o spinregs spinregs.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <OS.h>


static bigtime_t sEnd;


static status_t
spin(void* data)
{
	uint64_t n = (uint64_t)(addr_t)data;
	uint64_t x19 = 0x5a5a000000000000ULL | n;
	uint64_t low = 0xfedcba9876543200ULL | n;
	uint64_t high = 0x1111111111111111ULL * n;
	printf("thread %d: x19 %016llx, v31 %016llx%016llx\n", (int)find_thread(NULL),
		(unsigned long long)x19, (unsigned long long)high,
		(unsigned long long)low);
	fflush(stdout);
	while (system_time() < sEnd) {
		__asm__ volatile(
			"mov x19, %0\n"
			"mov v31.d[0], %1\n"
			"mov v31.d[1], %2\n"
			"mov x9, #0x30000\n"
			"1: subs x9, x9, #1\n"
			"b.ne 1b\n"
			: : "r"(x19), "r"(low), "r"(high) : "x9", "x19", "v31", "cc");
	}
	return B_OK;
}


int
main(int argc, char** argv)
{
	int count = argc > 1 ? atoi(argv[1]) : 3;
	int seconds = argc > 2 ? atoi(argv[2]) : 60;
	sEnd = system_time() + (bigtime_t)seconds * 1000000;
	thread_id threads[16];
	if (count > 16)
		count = 16;
	for (int i = 0; i < count; i++) {
		threads[i] = spawn_thread(spin, "spinregs", B_NORMAL_PRIORITY,
			(void*)(addr_t)(i + 1));
		resume_thread(threads[i]);
	}
	for (int i = 0; i < count; i++) {
		status_t result;
		wait_for_thread(threads[i], &result);
	}
	return 0;
}
