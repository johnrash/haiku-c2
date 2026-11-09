/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * bfsraw <file>...: where a BFS file's data is on its device (the block
 * runs of its inode, direct and indirect), and whether the device holds
 * what the file system returns for it: each 4 KiB page of the file is read
 * through the file system and straight from the partition (no file cache)
 * and compared. Reads only (root, for the partition device).
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fs_info.h>


#define INODE_MAGIC1 0x3bbe0ad9
#define SUPER_MAGIC1 0x42465331	/* 'BFS1' */

typedef struct {
	int32_t ag;
	uint16_t start;
	uint16_t length;
} __attribute__((packed)) block_run;

typedef struct {
	uint32_t blockSize;
	int32_t agShift;
} volume;

typedef struct {
	int64_t offset;		/* in the file */
	int64_t block;		/* on the partition */
	int64_t length;		/* bytes */
} extent;


static int64_t
run_block(const volume* v, block_run run)
{
	return ((int64_t)run.ag << v->agShift) + run.start;
}


static int
read_at(int fd, int64_t offset, void* buffer, size_t length)
{
	return pread(fd, buffer, length, offset) == (ssize_t)length ? 0 : -1;
}


static int
check_file(const char* path)
{
	struct stat st;
	if (stat(path, &st) != 0) {
		perror(path);
		return 1;
	}
	fs_info info;
	if (fs_stat_dev(st.st_dev, &info) != 0) {
		perror("fs_stat_dev");
		return 1;
	}
	int device = open(info.device_name, O_RDONLY);
	if (device < 0) {
		perror(info.device_name);
		return 1;
	}

	uint8_t super[512];
	if (read_at(device, 512, super, sizeof(super)) != 0
		|| *(int32_t*)(super + 32) != SUPER_MAGIC1) {
		fprintf(stderr, "%s: no BFS super block\n", info.device_name);
		close(device);
		return 1;
	}
	volume v;
	v.blockSize = *(uint32_t*)(super + 40);
	v.agShift = *(int32_t*)(super + 76);

	uint8_t* inode = malloc(v.blockSize);
	if (read_at(device, (int64_t)st.st_ino * v.blockSize, inode, v.blockSize)
			!= 0 || *(int32_t*)inode != INODE_MAGIC1) {
		fprintf(stderr, "%s: no inode at block %lld\n", path,
			(long long)st.st_ino);
		close(device);
		return 1;
	}
	const uint8_t* data = inode + 72;
	int64_t size = *(int64_t*)(data + 136);

	extent extents[1024];
	int count = 0;
	int64_t fileOffset = 0;
	for (int i = 0; i < 12; i++) {
		block_run run = ((const block_run*)data)[i];
		if (run.length == 0)
			break;
		extents[count].offset = fileOffset;
		extents[count].block = run_block(&v, run);
		extents[count].length = (int64_t)run.length * v.blockSize;
		fileOffset += extents[count].length;
		count++;
	}
	block_run indirect = *(const block_run*)(data + 104);
	if (indirect.length != 0) {
		size_t length = (size_t)indirect.length * v.blockSize;
		block_run* runs = malloc(length);
		if (read_at(device, run_block(&v, indirect) * v.blockSize, runs,
				length) == 0) {
			for (size_t i = 0; i < length / sizeof(block_run)
					&& runs[i].length != 0 && count < 1024; i++) {
				extents[count].offset = fileOffset;
				extents[count].block = run_block(&v, runs[i]);
				extents[count].length = (int64_t)runs[i].length * v.blockSize;
				fileOffset += extents[count].length;
				count++;
			}
		}
		free(runs);
	}
	if (*(const int64_t*)(data + 120) != 0 || ((const block_run*)(data + 120))->length != 0)
		printf("%s: has a double indirect range (not followed)\n", path);

	printf("%s: inode %lld, %lld bytes, block size %u, %d run(s):\n", path,
		(long long)st.st_ino, (long long)size, v.blockSize, count);
	for (int i = 0; i < count; i++) {
		printf("  file %8lld: block %lld, %lld bytes (partition offset %lld)\n",
			(long long)extents[i].offset, (long long)extents[i].block,
			(long long)extents[i].length,
			(long long)(extents[i].block * v.blockSize));
	}

	int file = open(path, O_RDONLY);
	uint8_t viaFs[4096];
	uint8_t viaDevice[4096];
	int differing = 0;
	for (int64_t page = 0; page * 4096 < size; page++) {
		int64_t offset = page * 4096;
		size_t length = size - offset < 4096 ? size - offset : 4096;
		if (pread(file, viaFs, length, offset) != (ssize_t)length)
			break;
		/* the page from the device, extent by extent */
		size_t done = 0;
		while (done < length) {
			int64_t want = offset + done;
			int e = 0;
			while (e < count && !(want >= extents[e].offset
					&& want < extents[e].offset + extents[e].length))
				e++;
			if (e == count)
				break;
			int64_t inExtent = want - extents[e].offset;
			size_t piece = length - done;
			if ((int64_t)piece > extents[e].length - inExtent)
				piece = extents[e].length - inExtent;
			if (read_at(device, extents[e].block * v.blockSize + inExtent,
					viaDevice + done, piece) != 0)
				break;
			done += piece;
		}
		if (done < length) {
			printf("  page %lld: not mapped by the runs\n", (long long)page);
			differing++;
			continue;
		}
		if (memcmp(viaFs, viaDevice, length) != 0) {
			if (differing < 10) {
				printf("  page %lld differs: the file system gives %02x%02x%02x"
					"%02x, the device %02x%02x%02x%02x\n", (long long)page,
					viaFs[0], viaFs[1], viaFs[2], viaFs[3], viaDevice[0],
					viaDevice[1], viaDevice[2], viaDevice[3]);
			}
			differing++;
		}
	}
	printf("  %d of %lld pages differ between the file system and the "
		"device\n", differing, (long long)((size + 4095) / 4096));
	close(file);
	close(device);
	free(inode);
	return differing ? 1 : 0;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: bfsraw <file>...\n");
		return 2;
	}
	int status = 0;
	for (int i = 1; i < argc; i++)
		status |= check_file(argv[i]);
	return status;
}
