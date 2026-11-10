/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * Decodes an MPEG-1/2, H.264 (Annex B, a name ending in .h264) or HEVC (a
 * name ending in .hevc) elementary stream with the S905's video decoder
 * (meson_vdec, milestone M26) and writes every frame's picture as NV12 (the Y
 * rows, then the CbCr rows, without the decoder's padding) to a file, for
 * tools/vdec-compare.py (tools/vdec-compare10.py for 10-bit HEVC) to check
 * against FFmpeg's decoding.
 *
 *   vdectest <firmware> <stream> <output> [<write size KB>]
 *
 * Prints the first frame's geometry, then the frames, the time and the
 * frames per second. A second thread writes the stream while this one takes
 * the frames: MPEG in pieces of <write size> KB (default 64), H.264 one
 * access unit at a time (with <write size>: in pieces too).
 * VDECTEST_NO_EOS=1 leaves out the end of the stream: the frames end with a
 * GET_FRAME that times out. VDECTEST_NO_OUTPUT=1 writes no frames, for
 * timing the decoder.
 *
 * Build: $CXX -O2 $HAIKU_CPPFLAGS -I$HAIKU_SRC/headers/private/drivers \
 *            -o vdectest vdectest.cpp
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <OS.h>

#include <meson_vdec.h>


static int sDevice;
static uint8* sStream;
static size_t sStreamSize;
static size_t sWriteSize = 64 * 1024;
static bool sH264;
static bool sHevc;
static bool sAccessUnits;


static uint8*
read_file(const char* path, size_t* _size)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		perror(path);
		return NULL;
	}
	struct stat st;
	fstat(fd, &st);
	uint8* data = (uint8*)malloc(st.st_size);
	if (data == NULL || read(fd, data, st.st_size) != st.st_size) {
		perror(path);
		close(fd);
		free(data);
		return NULL;
	}
	close(fd);
	*_size = st.st_size;
	return data;
}


/*!	The picture size of the stream's first MPEG sequence header (00 00 01
	B3: 12 bits of width, 12 of height).
*/
static bool
sequence_size(const uint8* data, size_t size, uint32* _width,
	uint32* _height)
{
	for (size_t i = 0; i + 7 <= size; i++) {
		if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1
			&& data[i + 3] == 0xb3) {
			*_width = (data[i + 4] << 4) | (data[i + 5] >> 4);
			*_height = ((data[i + 5] & 0xf) << 8) | data[i + 6];
			return true;
		}
	}
	return false;
}


/*!	Where the NAL unit at or after \a offset starts (its start code), and
	its type and the first byte after its header (H.264: one byte; HEVC:
	two).
*/
static size_t
next_nal(size_t offset, uint32* _type, uint8* _first)
{
	for (size_t i = offset; i + 5 < sStreamSize; i++) {
		if (sStream[i] == 0 && sStream[i + 1] == 0 && sStream[i + 2] == 1) {
			if (sHevc) {
				*_type = (sStream[i + 3] >> 1) & 0x3f;
				*_first = sStream[i + 5];
			} else {
				*_type = sStream[i + 3] & 0x1f;
				*_first = sStream[i + 4];
			}
			return i > offset && sStream[i - 1] == 0 ? i - 1 : i;
		}
	}
	return sStreamSize;
}


/*!	The end of the H.264 access unit that starts at \a offset: before the
	parameter sets, SEI or delimiter that follow a picture's slices, or the
	next picture's first slice (first_mb_in_slice 0: its first bit set).
*/
static size_t
access_unit_end(size_t offset)
{
	bool haveSlice = false;
	size_t position = offset;
	while (position < sStreamSize) {
		uint32 type;
		uint8 first;
		size_t nal = next_nal(position, &type, &first);
		if (nal >= sStreamSize)
			return sStreamSize;
		// H.264: slices 1 and 5; SEI, SPS, PPS, delimiter 6-9. HEVC: slices
		// 0-31; VPS, SPS, PPS, delimiter, prefix SEI 32-35, 39. A slice
		// starts a picture if its first bit (first_mb_in_slice 0,
		// first_slice_segment_in_pic_flag 1) is set.
		bool slice = sHevc ? type < 32 : type == 1 || type == 5;
		bool header = sHevc ? (type >= 32 && type <= 35) || type == 39
			: type >= 6 && type <= 9;
		if (haveSlice && ((slice && (first & 0x80) != 0) || header))
			return nal;
		haveSlice |= slice;
		position = nal + 4;
	}
	return sStreamSize;
}


static status_t
writer(void*)
{
	size_t next;
	for (size_t offset = 0; offset < sStreamSize; offset = next) {
		next = sAccessUnits ? access_unit_end(offset)
			: min_c(offset + sWriteSize, sStreamSize);
		meson_vdec_write piece;
		piece.data = sStream + offset;
		piece.size = next - offset;
		if (ioctl(sDevice, MESON_VDEC_WRITE, &piece, sizeof(piece)) != 0) {
			fprintf(stderr, "vdectest: write at %zu: %s\n", offset,
				strerror(errno));
			return errno;
		}
		if (getenv("VDECTEST_VERBOSE") != NULL) {
			printf("write: %zu bytes at %#x\n", piece.size,
				(unsigned)piece.offset);
		}
	}
	if (getenv("VDECTEST_NO_EOS") != NULL)
		return B_OK;
	if (ioctl(sDevice, MESON_VDEC_END_OF_STREAM, NULL, 0) != 0) {
		fprintf(stderr, "vdectest: end of stream: %s\n", strerror(errno));
		return errno;
	}
	return B_OK;
}


int
main(int argc, char** argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: vdectest <firmware> <stream> <output> "
			"[<write size KB>]\n");
		return 1;
	}
	size_t nameLength = strlen(argv[2]);
	sH264 = nameLength > 5 && strcmp(argv[2] + nameLength - 5, ".h264") == 0;
	sHevc = nameLength > 5 && strcmp(argv[2] + nameLength - 5, ".hevc") == 0;
	sAccessUnits = sH264 || sHevc;
	if (argc > 4) {
		sWriteSize = (size_t)atoi(argv[4]) * 1024;
		sAccessUnits = false;
	}

	size_t firmwareSize;
	uint8* firmware = read_file(argv[1], &firmwareSize);
	sStream = read_file(argv[2], &sStreamSize);
	if (firmware == NULL || sStream == NULL)
		return 1;
	FILE* output = fopen(argv[3], "wb");
	if (output == NULL) {
		perror(argv[3]);
		return 1;
	}

	sDevice = open(MESON_VDEC_DEVICE, O_RDWR);
	if (sDevice < 0) {
		perror(MESON_VDEC_DEVICE);
		return 1;
	}
	meson_vdec_start start;
	memset(&start, 0, sizeof(start));
	start.codec = sH264 ? MESON_VDEC_H264 : MESON_VDEC_MPEG12;
	if (sHevc)
		start.codec = MESON_VDEC_HEVC;
	start.firmware = firmware;
	start.firmware_size = firmwareSize;
	if (!sH264 && !sHevc
		&& !sequence_size(sStream, sStreamSize, &start.width, &start.height)) {
		fprintf(stderr, "vdectest: no MPEG sequence header\n");
		return 1;
	}
	printf("stream: %s %ux%u, %zu bytes\n",
		sHevc ? "HEVC" : sH264 ? "H.264" : "MPEG",
		(unsigned)start.width, (unsigned)start.height, sStreamSize);
	bigtime_t begin = system_time();
	if (ioctl(sDevice, MESON_VDEC_START, &start, sizeof(start)) != 0) {
		fprintf(stderr, "vdectest: start: %s\n", strerror(errno));
		return 1;
	}
	printf("started in %lld us: %u frames of %zu bytes\n",
		(long long)(system_time() - begin), (unsigned)start.frame_count,
		start.frame_size);

	// the frames' areas (H.264: one each), cloned when first seen
	struct mapping {
		area_id	source;
		uint8*	address;
	} mappings[256];
		// the driver's frame areas: new ones whenever the size changes
	uint32 mappingCount = 0;

	if (getenv("VDECTEST_START_ONLY") != NULL) {
		// the decoder started: does it stay up without a stream?
		snooze(2000000);
		ioctl(sDevice, MESON_VDEC_STOP, NULL, 0);
		printf("started and stopped\n");
		return 0;
	}

	begin = system_time();
	thread_id thread = spawn_thread(writer, "vdectest writer",
		B_NORMAL_PRIORITY, NULL);
	resume_thread(thread);

	bool writeFrames = getenv("VDECTEST_NO_OUTPUT") == NULL;
	uint32 count = 0;
	while (true) {
		meson_vdec_frame frame;
		memset(&frame, 0, sizeof(frame));
		frame.timeout = 3000000;
		if (ioctl(sDevice, MESON_VDEC_GET_FRAME, &frame, sizeof(frame))
				!= 0) {
			fprintf(stderr, "vdectest: frame %u: %s\n", (unsigned)count,
				strerror(errno));
			break;
		}
		if ((frame.flags & MESON_VDEC_FRAME_END_OF_STREAM) != 0)
			break;
		if ((frame.flags & MESON_VDEC_FRAME_GIVE_BACK) != 0) {
			// it holds none: the driver is about to go on
			continue;
		}
		if (count == 0) {
			printf("first frame after %lld us: %ux%u, %u bytes per row, "
				"CbCr at %u\n", (long long)(system_time() - begin),
				(unsigned)frame.width, (unsigned)frame.height,
				(unsigned)frame.bytes_per_row,
				(unsigned)frame.chroma_offset);
		}
		uint8* frames = NULL;
		for (uint32 i = 0; i < mappingCount; i++) {
			if (mappings[i].source == frame.frames)
				frames = mappings[i].address;
		}
		if (frames == NULL) {
			if (mappingCount == 256) {
				fprintf(stderr, "vdectest: too many frame areas\n");
				break;
			}
			area_id clone = clone_area("vdectest frames", (void**)&frames,
				B_ANY_ADDRESS, B_READ_AREA, frame.frames);
			if (clone < 0) {
				fprintf(stderr, "vdectest: clone_area: %s\n",
					strerror(clone));
				break;
			}
			mappings[mappingCount].source = frame.frames;
			mappings[mappingCount++].address = frames;
		}
		if (getenv("VDECTEST_VERBOSE") != NULL) {
			printf("frame %u: buffer %u, %ux%u, offset %#x%s\n",
				(unsigned)count, (unsigned)frame.index,
				(unsigned)frame.width, (unsigned)frame.height,
				(unsigned)frame.stream_offset,
				(frame.flags & MESON_VDEC_FRAME_DAMAGED) != 0
					? ", damaged" : "");
		}
		const uint8* base = frames + frame.offset;
		for (uint32 row = 0; writeFrames && row < frame.height; row++) {
			fwrite(base + (size_t)row * frame.bytes_per_row, 1, frame.width,
				output);
		}
		for (uint32 row = 0; writeFrames && row < frame.height / 2; row++) {
			fwrite(base + frame.chroma_offset
				+ (size_t)row * frame.bytes_per_row, 1, frame.width, output);
		}
		if (ioctl(sDevice, MESON_VDEC_RECYCLE_FRAME, &frame.index,
				sizeof(frame.index)) != 0) {
			fprintf(stderr, "vdectest: recycle %u: %s\n",
				(unsigned)frame.index, strerror(errno));
			break;
		}
		count++;
	}
	bigtime_t took = system_time() - begin;

	status_t result;
	wait_for_thread(thread, &result);
	ioctl(sDevice, MESON_VDEC_STOP, NULL, 0);
	close(sDevice);
	fclose(output);
	printf("%u frames in %lld ms: %.1f frames/s\n", (unsigned)count,
		(long long)took / 1000, count * 1000000.0 / took);
	return count > 0 ? 0 : 1;
}
