/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * rawfind <device> <start> <length> <needle file>...: offsets in
 * [start, start + length) of the device, at 2048-byte steps (BFS blocks),
 * where the first 4096 bytes of a needle file are. Reads only.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define STEP 2048
#define NEEDLE 4096
#define CHUNK (4 << 20)


int
main(int argc, char** argv)
{
	if (argc < 5) {
		fprintf(stderr, "usage: rawfind <device> <start> <length> "
			"<needle>...\n");
		return 2;
	}
	int device = open(argv[1], O_RDONLY);
	if (device < 0) {
		perror(argv[1]);
		return 1;
	}
	long long start = strtoll(argv[2], NULL, 0) & ~(long long)(STEP - 1);
	long long length = strtoll(argv[3], NULL, 0);
	int needles = argc - 4;
	uint8_t (*needle)[NEEDLE] = malloc((size_t)needles * NEEDLE);
	for (int i = 0; i < needles; i++) {
		FILE* f = fopen(argv[4 + i], "rb");
		if (f == NULL || fread(needle[i], 1, NEEDLE, f) != NEEDLE) {
			fprintf(stderr, "%s: need %d bytes\n", argv[4 + i], NEEDLE);
			return 1;
		}
		fclose(f);
	}

	uint8_t* buffer = malloc(CHUNK + NEEDLE);
	int found = 0;
	for (long long offset = start; offset < start + length; offset += CHUNK) {
		ssize_t got = pread(device, buffer, CHUNK + NEEDLE, offset);
		if (got < NEEDLE)
			break;
		for (ssize_t at = 0; at + NEEDLE <= got && at < CHUNK; at += STEP) {
			for (int i = 0; i < needles; i++) {
				if (buffer[at] == needle[i][0]
					&& memcmp(buffer + at, needle[i], NEEDLE) == 0) {
					printf("%s at %lld\n", argv[4 + i], offset + at);
					found++;
				}
			}
		}
	}
	printf("%d found\n", found);
	return 0;
}
