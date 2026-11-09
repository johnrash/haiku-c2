/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * crashtest [<depth>]: crashes on a NULL pointer write <depth> calls deep
 * (default 3), with a leaf function at the bottom, so that debug reports and
 * the Debugger show a known stack: main -> Recurse() x depth -> Leaf().
 * Build it with -g (DWARF) and without (call frame information only).
 */
#include <stdio.h>
#include <stdlib.h>


static volatile int* sNullPointer = NULL;


extern "C" __attribute__((noinline)) void
Leaf(int value)
{
	*sNullPointer = value;
}


extern "C" __attribute__((noinline)) int
Recurse(int depth, int value)
{
	if (depth == 0) {
		Leaf(value);
		return value;
	}
	int result = Recurse(depth - 1, value * 2 + 1);
	return result + depth;
}


int
main(int argc, char** argv)
{
	int depth = argc > 1 ? atoi(argv[1]) : 3;
	printf("crashtest: crashing %d calls deep\n", depth);
	fflush(stdout);
	return Recurse(depth, 1);
}
