/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 *   bfsgrow <mount point> <step MB> [readers]
 *
 * Grows a mounted BFS volume (BFS_IOCTL_RESIZE) by <step MB> at a time up
 * to the size of its device, while <readers> threads (default 4) read the
 * attributes of files on it, as Tracker does at boot, and one thread writes
 * (creates, appends to and deletes files). Resizing must not free the block
 * cache or the allocation groups under them (milestone M74). The volume needs
 * a grow limit (bfs_shell --initialize ... "grow_limit <size>"), so that it
 * grows in place, as c2-grow does on a boot volume.
 *
 * Build: $CXX -O2 -o bfsgrow bfsgrow.cpp -lbe
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Drivers.h>
#include <fs_attr.h>
#include <fs_info.h>
#include <OS.h>
#include <TypeConstants.h>


// from the BFS sources (src/add-ons/kernel/file_systems/bfs/bfs_control.h)
#define BFS_IOCTL_RESIZE	14205

struct resize_control {
	uint64	new_size;
	bool	dry_run;
};


static const int32 kFileCount = 200;
static const char* kAttribute = "bfsgrow:value";

static char sDirectory[B_PATH_NAME_LENGTH];
static volatile bool sDone = false;
static int64 sReads = 0;
static int64 sReadErrors = 0;
static int64 sWrites = 0;
static int64 sWriteErrors = 0;


static status_t
reader(void* data)
{
	int32 index = (int32)(addr_t)data;
	while (!sDone) {
		char path[B_PATH_NAME_LENGTH];
		snprintf(path, sizeof(path), "%s/f%" B_PRId32, sDirectory, index);
		index = (index + 7) % kFileCount;

		int fd = open(path, O_RDONLY);
		if (fd < 0) {
			atomic_add64(&sReadErrors, 1);
			continue;
		}
		attr_info info;
		int32 value;
		if (fs_stat_attr(fd, kAttribute, &info) != 0
			|| fs_read_attr(fd, kAttribute, B_INT32_TYPE, 0, &value,
				sizeof(value)) != (ssize_t)sizeof(value)) {
			atomic_add64(&sReadErrors, 1);
		}
		close(fd);
		atomic_add64(&sReads, 1);
	}
	return B_OK;
}


static status_t
writer(void*)
{
	char block[8192];
	memset(block, 0x5a, sizeof(block));
	int32 round = 0;
	while (!sDone) {
		char path[B_PATH_NAME_LENGTH];
		snprintf(path, sizeof(path), "%s/w%" B_PRId32, sDirectory, round % 16);
		int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd < 0) {
			atomic_add64(&sWriteErrors, 1);
			snooze(1000);
			continue;
		}
		for (int i = 0; i < 16; i++) {
			if (write(fd, block, sizeof(block)) != (ssize_t)sizeof(block))
				atomic_add64(&sWriteErrors, 1);
		}
		fs_write_attr(fd, kAttribute, B_INT32_TYPE, 0, &round, sizeof(round));
		close(fd);
		if (round % 4 == 3)
			unlink(path);
		round++;
		atomic_add64(&sWrites, 1);
	}
	return B_OK;
}


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: bfsgrow <mount point> <step MB> [readers]\n");
		return 2;
	}
	const char* mountPoint = argv[1];
	off_t step = (off_t)atoi(argv[2]) << 20;
	int32 readerCount = argc > 3 ? atoi(argv[3]) : 4;

	fs_info volume;
	if (fs_stat_dev(dev_for_path(mountPoint), &volume) != 0
		|| strcmp(volume.fsh_name, "bfs") != 0) {
		fprintf(stderr, "bfsgrow: %s is not a BFS volume\n", mountPoint);
		return 1;
	}
	int deviceFD = open(volume.device_name, O_RDONLY);
	device_geometry geometry;
	if (deviceFD < 0 || ioctl(deviceFD, B_GET_GEOMETRY, &geometry,
			sizeof(geometry)) != 0) {
		fprintf(stderr, "bfsgrow: %s: %s\n", volume.device_name,
			strerror(errno));
		return 1;
	}
	close(deviceFD);
	off_t deviceSize = (off_t)geometry.bytes_per_sector
		* geometry.sectors_per_track * geometry.cylinder_count
		* geometry.head_count;

	// the files the readers read
	snprintf(sDirectory, sizeof(sDirectory), "%s/bfsgrow", mountPoint);
	mkdir(sDirectory, 0755);
	for (int32 i = 0; i < kFileCount; i++) {
		char path[B_PATH_NAME_LENGTH];
		snprintf(path, sizeof(path), "%s/f%" B_PRId32, sDirectory, i);
		int fd = open(path, O_WRONLY | O_CREAT, 0644);
		if (fd < 0 || fs_write_attr(fd, kAttribute, B_INT32_TYPE, 0, &i,
				sizeof(i)) != (ssize_t)sizeof(i)) {
			fprintf(stderr, "bfsgrow: %s: %s\n", path, strerror(errno));
			return 1;
		}
		close(fd);
	}
	sync();

	thread_id threads[33];
	int32 threadCount = 0;
	for (int32 i = 0; i < readerCount && i < 32; i++) {
		threads[threadCount++] = spawn_thread(reader, "bfsgrow reader",
			B_NORMAL_PRIORITY, (void*)(addr_t)(i * 13 % kFileCount));
	}
	threads[threadCount++] = spawn_thread(writer, "bfsgrow writer",
		B_NORMAL_PRIORITY, NULL);
	for (int32 i = 0; i < threadCount; i++)
		resume_thread(threads[i]);

	int volumeFD = open(mountPoint, O_RDONLY);
	off_t size = volume.total_blocks * volume.block_size;
	int32 resizes = 0;
	status_t result = B_OK;
	bigtime_t start = system_time();
	printf("bfsgrow: %s on %s: %" B_PRIdOFF " MB, device %" B_PRIdOFF
		" MB, steps of %" B_PRIdOFF " MB, %" B_PRId32 " readers, 1 writer\n",
		mountPoint, volume.device_name, size >> 20, deviceSize >> 20,
		step >> 20, readerCount);
	fflush(stdout);
	while (size + step <= deviceSize) {
		size += step;
		resize_control control = { (uint64)size, false };
		if (ioctl(volumeFD, BFS_IOCTL_RESIZE, &control, sizeof(control))
				!= 0) {
			result = errno;
			printf("bfsgrow: growing to %" B_PRIdOFF " MB: %s\n", size >> 20,
				strerror(result));
			break;
		}
		resizes++;
		if (resizes % 10 == 0) {
			printf("bfsgrow: %" B_PRIdOFF " MB, %" B_PRId32 " resizes, %"
				B_PRId64 " reads, %" B_PRId64 " writes\n", size >> 20,
				resizes, sReads, sWrites);
			fflush(stdout);
		}
	}
	close(volumeFD);

	sDone = true;
	for (int32 i = 0; i < threadCount; i++) {
		status_t status;
		wait_for_thread(threads[i], &status);
	}

	fs_stat_dev(dev_for_path(mountPoint), &volume);
	printf("bfsgrow: %" B_PRId32 " resizes in %.1f s, now %" B_PRIdOFF
		" MB; %" B_PRId64 " reads (%" B_PRId64 " failed), %" B_PRId64
		" writes (%" B_PRId64 " failed)\n", resizes,
		(system_time() - start) / 1e6,
		(volume.total_blocks * volume.block_size) >> 20, sReads, sReadErrors,
		sWrites, sWriteErrors);
	return result == B_OK && sReadErrors == 0 && sWriteErrors == 0 ? 0 : 1;
}
