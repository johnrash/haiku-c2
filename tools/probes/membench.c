/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * membench: memcpy(), memmove() and memset() throughput in MB/s, for sizes
 * from 64 bytes to 16 MiB, with source and destination aligned alike and
 * not (offsets 1 and 3: the generic C memcpy copied those byte by byte).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <OS.h>


static const size_t kSizes[] = { 64, 1024, 16384, 262144, 4194304, 16777216 };
static const size_t kTotal = 256 * 1024 * 1024;
	// bytes moved per measurement

typedef void (*test_function)(char* dst, const char* src, size_t size);

static void test_memcpy(char* dst, const char* src, size_t size)
{
	memcpy(dst, src, size);
}

static void test_memmove(char* dst, const char* src, size_t size)
{
	memmove(dst, src, size);
}

static void test_memset(char* dst, const char* src, size_t size)
{
	memset(dst, src[0], size);
}


static double
measure(test_function function, char* dst, const char* src, size_t size)
{
	size_t rounds = kTotal / size;
	if (rounds > 4 * 1024 * 1024)
		rounds = 4 * 1024 * 1024;
	bigtime_t start = system_time();
	for (size_t i = 0; i < rounds; i++) {
		function(dst, src, size);
		__asm__ __volatile__("" : : "r"(dst) : "memory");
	}
	bigtime_t elapsed = system_time() - start;
	return (double)size * rounds / elapsed;
		// bytes per microsecond = MB/s
}


int
main()
{
	size_t bufferSize = 16777216 + 64;
	char* src = (char*)malloc(bufferSize);
	char* dst = (char*)malloc(bufferSize);
	if (src == NULL || dst == NULL)
		return 1;
	memset(src, 0x5a, bufferSize);
	memset(dst, 0, bufferSize);

	static const struct {
		const char* name;
		test_function function;
	} kTests[] = {
		{ "memcpy", test_memcpy },
		{ "memmove", test_memmove },
		{ "memset", test_memset },
	};
	static const struct {
		int dst, src;
	} kOffsets[] = { { 0, 0 }, { 0, 1 }, { 3, 1 } };

	printf("%-8s %-6s", "", "offs");
	for (size_t i = 0; i < sizeof(kSizes) / sizeof(kSizes[0]); i++)
		printf(" %9zu", kSizes[i]);
	printf("   (MB/s)\n");
	for (size_t t = 0; t < 3; t++) {
		for (size_t o = 0; o < 3; o++) {
			if (kTests[t].function == test_memset && kOffsets[o].src != 0)
				continue;
			printf("%-8s %d/%d   ", kTests[t].name, kOffsets[o].dst,
				kOffsets[o].src);
			for (size_t i = 0; i < sizeof(kSizes) / sizeof(kSizes[0]); i++) {
				printf(" %9.0f", measure(kTests[t].function,
					dst + kOffsets[o].dst, src + kOffsets[o].src, kSizes[i]));
				fflush(stdout);
			}
			printf("\n");
		}
	}
	return 0;
}
