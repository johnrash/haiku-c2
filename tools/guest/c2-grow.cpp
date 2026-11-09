/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * First-boot growing of the boot volume, run by c2-system.sh at boot when the
 * image has the c2-grow marker (tools/image-add-diag.sh --grow-on-boot).
 *
 *   c2-grow            do the next step
 *   c2-grow --check    only print what the next step would be
 *
 * The steps, one per boot:
 *  1. partition: if the boot partition is the last one in the disk's MBR and
 *     unused space follows it, its entry is extended to the end of the disk
 *     (exit status 2: reboot, Haiku keeps the size of a mounted partition);
 *  2. file system: if the BFS volume is smaller than its partition, it grows
 *     into it (BFS_IOCTL_RESIZE), but only as far as it can without moving
 *     anything: images made with a BFS grow limit (HAIKU_IMAGE_GROW_LIMIT)
 *     have their log behind a block bitmap sized for that limit;
 *  3. nothing left: exit status 0, and c2-system.sh removes the marker.
 * Each step prints one line for the log. Exit status 1: an error.
 *
 * Build: $CXX -O2 $HAIKU_CPPFLAGS -o c2-grow c2-grow.cpp -lbe
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Drivers.h>
#include <fs_info.h>


// from the BFS sources (src/add-ons/kernel/file_systems/bfs/bfs_control.h)
#define BFS_IOCTL_RESIZE	14205

struct resize_control {
	uint64	new_size;
	bool	dry_run;
};


static const off_t kSectorSize = 512;
static const off_t kMinimumGain = 16 * 1024 * 1024;
	// less unused space than this is not worth a reboot
static const uint8 kBFSPartitionType = 0xeb;


static uint32
read_le32(const uint8* data)
{
	return data[0] | (data[1] << 8) | (data[2] << 16)
		| ((uint32)data[3] << 24);
}


static void
write_le32(uint8* data, uint32 value)
{
	data[0] = value;
	data[1] = value >> 8;
	data[2] = value >> 16;
	data[3] = value >> 24;
}


static int
fail(const char* what, status_t error)
{
	printf("c2-grow: %s: %s\n", what, strerror(error));
	return 1;
}


int
main(int argc, char** argv)
{
	bool check = argc > 1 && strcmp(argv[1], "--check") == 0;

	// the boot volume and its partition
	fs_info volume;
	if (fs_stat_dev(dev_for_path("/boot"), &volume) != 0)
		return fail("boot volume", errno);
	if (strcmp(volume.fsh_name, "bfs") != 0) {
		printf("c2-grow: the boot volume is not BFS\n");
		return 1;
	}

	int partitionFD = open(volume.device_name, O_RDONLY);
	if (partitionFD < 0)
		return fail(volume.device_name, errno);
	partition_info partition;
	if (ioctl(partitionFD, B_GET_PARTITION_INFO, &partition,
			sizeof(partition)) != 0) {
		return fail("partition info", errno);
	}

	// the BFS superblock: the log's position limits growing in place
	uint8 superBlock[512];
	if (pread(partitionFD, superBlock, sizeof(superBlock), 512)
			!= (ssize_t)sizeof(superBlock)) {
		return fail("BFS superblock", errno);
	}
	close(partitionFD);
	uint32 blockSize = read_le32(superBlock + 40);
	uint32 groupShift = read_le32(superBlock + 76);
	uint32 logGroup = read_le32(superBlock + 88);
	uint32 logStart = superBlock[92] | (superBlock[93] << 8);
	uint32 logLength = superBlock[94] | (superBlock[95] << 8);
	off_t logBlock = ((off_t)logGroup << groupShift) + logStart;
	off_t inPlaceLimit = (logBlock - 1) * blockSize * 8 * (off_t)blockSize;
		// the bitmap blocks in front of the log cover this many bytes
	if (logLength < 4096) {
		// a volume over 1 GB needs a 4096 block log (Volume::
		// CalculateLogSize()): only images made with a grow limit have one
		inPlaceLimit = 0;
	}

	// the disk
	int diskFD = open(partition.device, check ? O_RDONLY : O_RDWR);
	if (diskFD < 0)
		return fail(partition.device, errno);
	device_geometry geometry;
	if (ioctl(diskFD, B_GET_GEOMETRY, &geometry, sizeof(geometry)) != 0)
		return fail("disk geometry", errno);
	off_t diskSize = (off_t)geometry.bytes_per_sector
		* geometry.sectors_per_track * geometry.cylinder_count
		* geometry.head_count;

	// 1. the partition
	uint8 mbr[kSectorSize];
	if (pread(diskFD, mbr, sizeof(mbr), 0) != (ssize_t)sizeof(mbr))
		return fail("MBR", errno);
	if (mbr[510] != 0x55 || mbr[511] != 0xaa) {
		printf("c2-grow: the disk has no MBR\n");
		return 1;
	}
	int entry = -1;
	off_t lastStart = 0;
	for (int i = 0; i < 4; i++) {
		uint8* e = mbr + 446 + 16 * i;
		if (e[4] == 0)
			continue;
		if (e[4] == 0xee || e[4] == 0x05 || e[4] == 0x0f || e[4] == 0x85) {
			printf("c2-grow: GPT or extended partitions: not touched\n");
			return 0;
		}
		off_t start = (off_t)read_le32(e + 8) * kSectorSize;
		if (start > lastStart)
			lastStart = start;
		if (start == partition.offset && e[4] == kBFSPartitionType)
			entry = i;
	}
	if (entry < 0) {
		printf("c2-grow: no MBR entry for the boot partition\n");
		return 1;
	}

	off_t partitionEnd = partition.offset + partition.size;
	off_t newEnd = diskSize / 4096 * 4096;
	if (partition.offset == lastStart && newEnd - partitionEnd >= kMinimumGain) {
		off_t newSize = newEnd - partition.offset;
		printf("c2-grow: partition %d: %" B_PRIdOFF " MB -> %" B_PRIdOFF
			" MB%s\n", entry + 1, partition.size >> 20, newSize >> 20,
			check ? " (check only)" : "; reboot, then the file system grows");
		if (check)
			return 0;

		write_le32(mbr + 446 + 16 * entry + 12, newSize / kSectorSize);
		if (pwrite(diskFD, mbr, sizeof(mbr), 0) != (ssize_t)sizeof(mbr))
			return fail("writing the MBR", errno);
		fsync(diskFD);
		return 2;
	}
	close(diskFD);

	// 2. the file system
	off_t volumeSize = volume.total_blocks * volume.block_size;
	off_t targetSize = partition.size / blockSize * blockSize;
	if (targetSize > inPlaceLimit) {
		// only in place: moving the log and the root directory of the
		// mounted boot volume is more than the resize code is tested for
		targetSize = inPlaceLimit / blockSize * blockSize;
	}
	if (targetSize - volumeSize < kMinimumGain) {
		printf("c2-grow: nothing to grow (volume %" B_PRIdOFF " MB, partition %"
			B_PRIdOFF " MB, in place up to %" B_PRIdOFF " MB)\n",
			volumeSize >> 20, partition.size >> 20, inPlaceLimit >> 20);
		return 0;
	}

	printf("c2-grow: file system: %" B_PRIdOFF " MB -> %" B_PRIdOFF " MB%s\n",
		volumeSize >> 20, targetSize >> 20, check ? " (check only)" : "");
	if (check)
		return 0;

	int volumeFD = open("/boot", O_RDONLY);
	if (volumeFD < 0)
		return fail("/boot", errno);
	sync();
	resize_control control = { (uint64)targetSize, false };
	if (ioctl(volumeFD, BFS_IOCTL_RESIZE, &control, sizeof(control)) != 0)
		return fail("growing the file system", errno);
	sync();
	close(volumeFD);
	return 0;
}
