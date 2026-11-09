/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * fillfs <dir> [<rounds>]: fills a volume with small files and directories
 * until it runs out of space (BFS then aborts sub transactions), deletes
 * every second file, and fills again; prints the counts. Run it on a RAM
 * disk (ramdisk create -s 24M; mkfs -t bfs; mount), then checkfs.
 *
 * Build: $CC -O2 $HAIKU_CPPFLAGS -o fillfs fillfs.c
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char sBuffer[16384];

static int fill(const char* dir, const char* prefix, int* created)
{
	int failures = 0, i = 0;
	char path[1024];
	while (failures < 200) {
		i++;
		snprintf(path, sizeof(path), "%s/%s%d", dir, prefix, i);
		if (i % 5 == 0) {
			if (mkdir(path, 0755) != 0)
				failures++;
			continue;
		}
		int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0) {
			failures++;
			continue;
		}
		size_t size = 500 + (rand() % 12000);
		ssize_t written = write(fd, sBuffer, size);
		close(fd);
		if (written != (ssize_t)size) {
			failures++;
			unlink(path);
			continue;
		}
		(*created)++;
	}
	return i;
}

int main(int argc, char** argv)
{
	const char* dir = argv[1];
	int rounds = argc > 2 ? atoi(argv[2]) : 2;
	for (size_t i = 0; i < sizeof(sBuffer); i++)
		sBuffer[i] = rand();
	for (int round = 0; round < rounds; round++) {
		int created = 0;
		char prefix[16];
		snprintf(prefix, sizeof(prefix), "r%d_", round);
		int tried = fill(dir, prefix, &created);
		printf("round %d: %d files created of %d tries before running out\n", round, created, tried);
		fflush(stdout);
		// delete every second regular file
		DIR* d = opendir(dir);
		struct dirent* entry;
		int n = 0, deleted = 0;
		char path[1024];
		while ((entry = readdir(d)) != NULL) {
			if (entry->d_name[0] == '.')
				continue;
			snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
			struct stat st;
			if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && (n++ % 2) == 0) {
				if (unlink(path) == 0)
					deleted++;
			}
		}
		closedir(d);
		printf("round %d: %d files deleted\n", round, deleted);
		fflush(stdout);
	}
	return 0;
}
