/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * smallfiles <dir> [<count>]: creates <count> (2000) files of 8 KiB in
 * <dir>, without syncing, and prints the time it took.
 *
 * Build: $CC -O2 $HAIKU_CPPFLAGS -o smallfiles smallfiles.c
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <OS.h>
int main(int argc, char** argv)
{
	int count = argc > 2 ? atoi(argv[2]) : 2000;
	char buffer[8192], path[1024];
	memset(buffer, 0x5a, sizeof(buffer));
	bigtime_t start = system_time();
	for (int i = 0; i < count; i++) {
		snprintf(path, sizeof(path), "%s/f%05d", argv[1], i);
		int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		write(fd, buffer, sizeof(buffer));
		close(fd);
	}
	printf("%d files in %.3f s\n", count, (system_time() - start) / 1e6);
	return 0;
}
