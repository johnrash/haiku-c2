/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * physread <physical address> [<words>]: 32-bit words of physical memory or
 * device registers through /dev/misc/mem (root only), one aligned 4-byte
 * read each. Reads only; for registers without read side effects.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: physread <physical address> [<words>]\n");
		return 1;
	}
	uint64_t address = strtoull(argv[1], NULL, 0) & ~(uint64_t)3;
	int words = argc > 2 ? atoi(argv[2]) : 1;
	int fd = open("/dev/misc/mem", O_RDONLY);
	if (fd < 0) {
		perror("/dev/misc/mem");
		return 1;
	}
	for (int i = 0; i < words; i++) {
		uint32_t value;
		if (pread(fd, &value, sizeof(value), address + i * 4) != 4) {
			perror("read");
			return 1;
		}
		printf("%#010llx: %#010x\n", (unsigned long long)address + i * 4,
			value);
	}
	close(fd);
	return 0;
}
