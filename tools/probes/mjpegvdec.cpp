/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * mjpegvdec <gxl_mjpeg.bin> <file.jpg> [pictures] [out.yuv]: decodes a JPEG
 * picture (a webcam's MJPEG frame) with meson_vdec's MJPEG session, written
 * as many times as asked, one picture a WRITE. For each it prints when its
 * frame came (before the next WRITE, or only after it), and the time; the
 * first frame goes to out.yuv (NV12: Y, then CbCr; planar frames: Y, Cb,
 * Cr; no padding).
 * A planar first frame also goes to out.yuv.bgra as B_RGB32, converted
 * with the webcam add-on's i420_neon.c (build with it, -I its directory).
 * Then the end of the stream, and the frames that come after it. With
 * <cut every> (after out.yuv, "-" for none), every that many pictures one
 * is written cut short.
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>

#include <meson_vdec.h>

#include "i420_neon.h"


static uint8*
read_file(const char* path, size_t* _size)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;
	off_t size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	uint8* data = (uint8*)malloc(size);
	if (data == NULL || read(fd, data, size) != size) {
		close(fd);
		free(data);
		return NULL;
	}
	close(fd);
	*_size = size;
	return data;
}


/*!	The picture's size and its components' sampling, from its SOF0. */
static bool
parse_sof(const uint8* data, size_t size, uint32* _width, uint32* _height)
{
	size_t position = 2;
	while (position + 4 <= size) {
		if (data[position] != 0xff)
			return false;
		uint8 marker = data[position + 1];
		uint32 length = (data[position + 2] << 8) | data[position + 3];
		if (marker == 0xc0 || marker == 0xc1) {
			const uint8* sof = data + position + 4;
			*_height = (sof[1] << 8) | sof[2];
			*_width = (sof[3] << 8) | sof[4];
			printf("SOF%d: %ux%u, %d components:", marker - 0xc0,
				(unsigned)*_width, (unsigned)*_height, sof[5]);
			for (int i = 0; i < sof[5]; i++) {
				printf(" %d %dx%d q%d", sof[6 + 3 * i], sof[7 + 3 * i] >> 4,
					sof[7 + 3 * i] & 0xf, sof[8 + 3 * i]);
			}
			printf("\n");
			return true;
		}
		position += 2 + length;
	}
	return false;
}


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: mjpegvdec <gxl_mjpeg.bin> <file.jpg> "
			"[pictures] [out.yuv|-] [cut every]\n");
		return 1;
	}
	int pictures = argc > 3 ? atoi(argv[3]) : 1;
	const char* outPath = argc > 4 && strcmp(argv[4], "-") != 0
		? argv[4] : NULL;
	// every that many pictures, one cut short (60% of it), as a lost
	// isochronous packet would leave it
	int cutEvery = argc > 5 ? atoi(argv[5]) : 0;

	size_t firmwareSize;
	uint8* firmware = read_file(argv[1], &firmwareSize);
	size_t jpegSize;
	uint8* jpeg = read_file(argv[2], &jpegSize);
	if (firmware == NULL || jpeg == NULL) {
		fprintf(stderr, "can't read the files\n");
		return 1;
	}
	uint32 width, height;
	if (!parse_sof(jpeg, jpegSize, &width, &height)) {
		fprintf(stderr, "no SOF0\n");
		return 1;
	}

	int fd = open(MESON_VDEC_DEVICE, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open: %s\n", strerror(errno));
		return 1;
	}
	meson_vdec_start start = {};
	start.codec = MESON_VDEC_MJPEG;
	start.firmware = firmware;
	start.firmware_size = firmwareSize;
	start.width = width;
	start.height = height;
	if (ioctl(fd, MESON_VDEC_START, &start, sizeof(start)) != 0) {
		fprintf(stderr, "START: %s\n", strerror(errno));
		return 1;
	}
	printf("started: %u frames of %zu bytes\n", (unsigned)start.frame_count,
		start.frame_size);
	uint8* frames;
	area_id area = clone_area("mjpegvdec frames", (void**)&frames,
		B_ANY_ADDRESS, B_READ_AREA, start.frames);
	if (area < 0) {
		fprintf(stderr, "clone_area: %s\n", strerror(area));
		return 1;
	}

	int received = 0;
	int late = 0;
	uint32 firstHash = 0;
	int differing = 0;
	bigtime_t decodeTotal = 0;
	for (int i = 0; i < pictures; i++) {
		meson_vdec_write write = {};
		write.data = jpeg;
		write.size = jpegSize;
		bool cut = cutEvery > 0 && i % cutEvery == cutEvery - 1;
		if (cut)
			write.size = jpegSize * 6 / 10;
		bigtime_t written = system_time();
		if (ioctl(fd, MESON_VDEC_WRITE, &write, sizeof(write)) != 0) {
			fprintf(stderr, "WRITE %d: %s\n", i, strerror(errno));
			break;
		}
		bigtime_t taken = system_time();

		// its frame, before the next picture is written?
		meson_vdec_frame frame = {};
		frame.timeout = 200000;
		if (ioctl(fd, MESON_VDEC_GET_FRAME, &frame, sizeof(frame)) != 0) {
			if (i < 3 || i == pictures - 1 || cut || cutEvery > 0)
				printf("picture %d%s: no frame within 200 ms\n", i,
					cut ? " (cut short)" : "");
			late++;
			continue;
		}
		bigtime_t now = system_time();
		decodeTotal += now - taken;
		received++;
		if (i < 3 || i == pictures - 1 || cut) {
			printf("picture %d%s (offset %u): frame %u, %ux%u, %u bytes a row, "
				"chroma at %u, flags %#x, stream offset %u; write %lld us, "
				"frame %lld us after it\n", i, cut ? " (cut short)" : "",
				(unsigned)write.offset,
				(unsigned)frame.index, (unsigned)frame.width,
				(unsigned)frame.height, (unsigned)frame.bytes_per_row,
				(unsigned)frame.chroma_offset, (unsigned)frame.flags,
				(unsigned)frame.stream_offset, (long long)(taken - written),
				(long long)(now - taken));
		}
		if (received == 1 && outPath != NULL) {
			const uint8* base = frames + frame.offset;
			FILE* out = fopen(outPath, "wb");
			for (uint32 y = 0; y < frame.height; y++)
				fwrite(base + y * frame.bytes_per_row, 1, frame.width, out);
			if (frame.cr_offset != 0) {
				// planar: Cb, then Cr
				uint32 offsets[] = { frame.chroma_offset, frame.cr_offset };
				for (int p = 0; p < 2; p++) {
					for (uint32 y = 0; y < frame.height / 2; y++) {
						fwrite(base + offsets[p]
							+ y * (frame.bytes_per_row / 2), 1,
							frame.width / 2, out);
					}
				}
			} else {
				for (uint32 y = 0; y < frame.height / 2; y++) {
					fwrite(base + frame.chroma_offset
						+ y * frame.bytes_per_row, 1, frame.width, out);
				}
			}
			fclose(out);
			printf("wrote %s\n", outPath);
			if (frame.cr_offset != 0) {
				// and as B_RGB32, with the webcam add-on's converter
				size_t rowBytes = frame.width * 4;
				uint8* rgb = (uint8*)malloc(rowBytes * frame.height);
				bigtime_t converting = system_time();
				for (int k = 0; k < 10; k++) {
					i420_to_rgb32_neon(rgb, rowBytes, base,
						frame.bytes_per_row, base + frame.chroma_offset,
						base + frame.cr_offset, frame.bytes_per_row / 2,
						frame.width, frame.height);
				}
				converting = (system_time() - converting) / 10;
				char path[B_PATH_NAME_LENGTH];
				snprintf(path, sizeof(path), "%s.bgra", outPath);
				out = fopen(path, "wb");
				fwrite(rgb, 1, rowBytes * frame.height, out);
				fclose(out);
				free(rgb);
				printf("wrote %s: %lld us a conversion\n", path,
					(long long)converting);
			}
		}
		// the same picture each time: the same frame
		{
			const uint8* base = frames + frame.offset;
			uint32 hash = 2166136261u;
			size_t size = frame.cr_offset != 0
				? frame.cr_offset + frame.bytes_per_row / 2 * frame.height / 2
				: frame.chroma_offset + frame.bytes_per_row * frame.height / 2;
			for (size_t k = 0; k < size; k += 61)
				hash = (hash ^ base[k]) * 16777619u;
			if (received == 1)
				firstHash = hash;
			else if (hash != firstHash) {
				printf("picture %d: frame differs from the first\n", i);
				if (differing++ == 0 && outPath != NULL) {
					char path[B_PATH_NAME_LENGTH];
					snprintf(path, sizeof(path), "%s.diff", outPath);
					FILE* out = fopen(path, "wb");
					fwrite(base, 1, size, out);
					fclose(out);
				}
			}
		}
		uint32 index = frame.index;
		ioctl(fd, MESON_VDEC_RECYCLE_FRAME, &index, sizeof(index));
	}

	ioctl(fd, MESON_VDEC_END_OF_STREAM, NULL, 0);
	int after = 0;
	while (true) {
		meson_vdec_frame frame = {};
		frame.timeout = 1000000;
		if (ioctl(fd, MESON_VDEC_GET_FRAME, &frame, sizeof(frame)) != 0) {
			printf("after the end: %s\n", strerror(errno));
			break;
		}
		if ((frame.flags & MESON_VDEC_FRAME_END_OF_STREAM) != 0) {
			printf("end of stream\n");
			break;
		}
		after++;
		uint32 index = frame.index;
		ioctl(fd, MESON_VDEC_RECYCLE_FRAME, &index, sizeof(index));
	}
	printf("%d pictures: %d frames before the next picture (%.2f ms each "
		"after its write), %d not, %d after the end; %d differ from the "
		"first\n", pictures, received,
		received > 0 ? decodeTotal / 1000.0 / received : 0.0, late, after,
		differing);

	ioctl(fd, MESON_VDEC_STOP, NULL, 0);
	delete_area(area);
	close(fd);
	return 0;
}
