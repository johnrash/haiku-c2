/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * fsbench <directory> [<MiB> [<raw device>]]: file system throughput in a
 * scratch folder it creates in <directory> and removes again:
 * - sequential write of <MiB> (default 128) in 1 MiB writes, fsync;
 * - sequential read of it (from the cache, mostly);
 * - 2000 files of 8 KiB, then sync; reading them; deleting them, sync;
 * - 2000 random 4 KiB writes into a file of 64 MiB (or <MiB>, if less),
 *   fsync;
 * - with <raw device>, an uncached sequential read of <MiB> from it.
 * Prints MB/s (and files/s); returns 1 on an error.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>


static double
seconds_since(bigtime_t start)
{
	return (system_time() - start) / 1000000.0;
}


static int
fail(const char* what, const char* path)
{
	printf("fsbench: %s %s: %s\n", what, path, strerror(errno));
	return 1;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		printf("usage: fsbench <directory> [<MiB> [<raw device>]]\n");
		return 1;
	}
	size_t megabytes = argc > 2 ? atoi(argv[2]) : 128;
	char dir[1024], path[1100];
	snprintf(dir, sizeof(dir), "%s/fsbench.%d", argv[1], (int)getpid());
	if (mkdir(dir, 0755) != 0)
		return fail("mkdir", dir);

	size_t blockSize = 1024 * 1024;
	char* buffer = malloc(blockSize);
	for (size_t i = 0; i < blockSize; i++)
		buffer[i] = (char)(i * 31 + (i >> 12));

	// sequential write
	snprintf(path, sizeof(path), "%s/big", dir);
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return fail("create", path);
	bigtime_t start = system_time();
	for (size_t i = 0; i < megabytes; i++) {
		if (write(fd, buffer, blockSize) != (ssize_t)blockSize)
			return fail("write", path);
	}
	double cached = seconds_since(start);
	fsync(fd);
	double total = seconds_since(start);
	close(fd);
	printf("sequential write: %zu MiB, %.1f MB/s (%.1f MB/s into the cache, "
		"fsync %.2f s)\n", megabytes, megabytes * 1.048576 / total,
		megabytes * 1.048576 / cached, total - cached);

	// sequential read
	fd = open(path, O_RDONLY);
	start = system_time();
	while (read(fd, buffer, blockSize) > 0)
		;
	total = seconds_since(start);
	close(fd);
	printf("sequential read: %.1f MB/s\n", megabytes * 1.048576 / total);
	unlink(path);
	sync();

	// small files
	const int fileCount = 2000;
	const size_t fileSize = 8192;
	start = system_time();
	for (int i = 0; i < fileCount; i++) {
		snprintf(path, sizeof(path), "%s/small%04d", dir, i);
		fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0 || write(fd, buffer + i, fileSize) != (ssize_t)fileSize)
			return fail("write", path);
		close(fd);
	}
	cached = seconds_since(start);
	sync();
	total = seconds_since(start);
	printf("small files: %d x %zu KiB in %.2f s, %.0f files/s, %.1f MB/s "
		"(%.2f s of it sync)\n", fileCount, fileSize / 1024, total,
		fileCount / total, fileCount * fileSize / 1000000.0 / total,
		total - cached);
	start = system_time();
	for (int i = 0; i < fileCount; i++) {
		snprintf(path, sizeof(path), "%s/small%04d", dir, i);
		fd = open(path, O_RDONLY);
		if (fd < 0 || read(fd, buffer, fileSize) != (ssize_t)fileSize)
			return fail("read", path);
		close(fd);
	}
	printf("small files read: %.0f files/s\n",
		fileCount / seconds_since(start));
	start = system_time();
	for (int i = 0; i < fileCount; i++) {
		snprintf(path, sizeof(path), "%s/small%04d", dir, i);
		unlink(path);
	}
	sync();
	printf("small files delete: %.0f files/s\n",
		fileCount / seconds_since(start));

	// random 4 KiB writes
	snprintf(path, sizeof(path), "%s/random", dir);
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	int randomMegabytes = megabytes < 64 ? (int)megabytes : 64;
	for (int i = 0; i < randomMegabytes; i++)
		write(fd, buffer, blockSize);
	fsync(fd);
	srand(1);
	start = system_time();
	for (int i = 0; i < 2000; i++) {
		off_t offset = (off_t)(rand() % (randomMegabytes * 256)) * 4096;
		if (pwrite(fd, buffer + i, 4096, offset) != 4096)
			return fail("pwrite", path);
	}
	fsync(fd);
	total = seconds_since(start);
	close(fd);
	printf("random 4 KiB writes: 2000 in %.2f s, %.0f IOPS\n", total,
		2000 / total);
	unlink(path);
	rmdir(dir);
	sync();

	if (argc > 3) {
		fd = open(argv[3], O_RDONLY);
		if (fd < 0)
			return fail("open", argv[3]);
		start = system_time();
		for (size_t i = 0; i < megabytes; i++) {
			if (read(fd, buffer, blockSize) != (ssize_t)blockSize)
				return fail("read", argv[3]);
		}
		total = seconds_since(start);
		close(fd);
		printf("raw read: %.1f MB/s\n", megabytes * 1.048576 / total);
	}
	return 0;
}
