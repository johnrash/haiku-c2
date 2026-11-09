/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * dbgtarget: the debuggee for dbgtest. Prints the addresses of c2_target()
 * and gCounter, then calls c2_target(i) every 200 ms and prints the result.
 * Build with -O0, so that c2_target() has many instructions.
 */
#include <stdio.h>

#include <OS.h>


volatile long gCounter;


long
c2_target(long x)
{
	long y = x * 3;
	y += 7;
	gCounter += y;
	y ^= 5;
	y -= 2;
	y *= 2;
	y += 1;
	return y;
}


int
main()
{
	printf("dbgtarget: team %" B_PRId32 " c2_target %p gCounter %p\n",
		getpid(), (void*)c2_target, (void*)&gCounter);
	fflush(stdout);
	for (long i = 0; ; i++) {
		long result = c2_target(i);
		printf("%ld -> %ld\n", i, result);
		fflush(stdout);
		snooze(200000);
	}
	return 0;
}
