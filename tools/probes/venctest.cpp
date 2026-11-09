/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * venctest <microcode> <width> <height> <frames> <out.h264> [options]:
 * the S905's H.264 encoder through meson_venc (milestone M48 (docs/milestones.md)). Encodes a test
 * picture that moves (a gradient, a bright square crossing it, and four
 * colored quadrants for telling the RGB formats' byte orders apart) and
 * writes the stream (Annex B, the SPS and PPS first) for decoding with
 * FFmpeg on the laptop. Prints each picture's size and the hardware's time.
 * Options: nv12 (default), nv21, rgb24 or rgb32 (B_RGB32); qp <n> (default
 * 28);
 * gop <n> (an IDR every n pictures, default 30); nr (noise reduction);
 * noise (grain on every pixel, as a camera's: harder to encode);
 * raw <file> (the input pictures too, as the device read them).
 * Build: $CXX -O2 $HAIKU_CPPFLAGS -I<haiku>/headers/private/drivers -o
 * venctest venctest.cpp
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>

#include <meson_venc.h>


static bool sNoise = false;


static void
fill_picture(uint8* base, const meson_venc_start& start, uint32 format,
	int frame)
{
	uint32 seed = frame * 2654435761u;
	uint32 width = start.width;
	uint32 height = start.height;
	uint32 square = width / 8;
	uint32 squareX = (frame * 8) % (width - square);
	uint32 squareY = height / 3 + (frame * 3) % (height / 3);

	for (uint32 y = 0; y < height; y++) {
		for (uint32 x = 0; x < width; x++) {
			// luma-ish value, and a color per quadrant
			uint32 value = (x * 160 / width + y * 64 / height + 16) & 0xff;
			bool inSquare = x >= squareX && x < squareX + square
				&& y >= squareY && y < squareY + square;
			uint8 r, g, b;
			if (inSquare) {
				r = g = b = 235;
			} else if (y < height / 6) {
				// the top band: red, green, blue, white quarters
				int quarter = x * 4 / width;
				r = quarter == 0 || quarter == 3 ? 200 : 20;
				g = quarter == 1 || quarter == 3 ? 200 : 20;
				b = quarter == 2 || quarter == 3 ? 200 : 20;
			} else {
				r = g = b = value;
			}
			if (sNoise) {
				// a camera's grain: every pixel a little off
				seed = seed * 1103515245 + 12345;
				int noise = ((seed >> 16) & 15) - 8;
				r = (uint8)max_c(0, min_c(255, r + noise));
				g = (uint8)max_c(0, min_c(255, g + noise));
				b = (uint8)max_c(0, min_c(255, b + noise));
			}

			switch (format) {
				case MESON_VENC_NV12:
				case MESON_VENC_NV21:
				{
					int luma = (66 * r + 129 * g + 25 * b + 128) / 256 + 16;
					base[y * start.bytes_per_row + x] = luma;
					if ((x & 1) == 0 && (y & 1) == 0) {
						int cb = (-38 * r - 74 * g + 112 * b + 128) / 256 + 128;
						int cr = (112 * r - 94 * g - 18 * b + 128) / 256 + 128;
						uint8* chroma = base + start.chroma_offset
							+ (y / 2) * start.bytes_per_row + x;
						chroma[format == MESON_VENC_NV12 ? 0 : 1] = cb;
						chroma[format == MESON_VENC_NV12 ? 1 : 0] = cr;
					}
					break;
				}
				case MESON_VENC_RGB24:
				{
					uint8* pixel = base + y * start.bytes_per_row + x * 3;
					pixel[0] = r;
					pixel[1] = g;
					pixel[2] = b;
					break;
				}
				case MESON_VENC_RGB32:
				{
					// B_RGB32
					uint8* pixel = base + y * start.bytes_per_row + x * 4;
					pixel[0] = b;
					pixel[1] = g;
					pixel[2] = r;
					pixel[3] = 255;
					break;
				}
			}
		}
	}
}


int
main(int argc, char** argv)
{
	if (argc < 6) {
		fprintf(stderr, "usage: venctest <microcode> <width> <height> "
			"<frames> <out.h264> [nv12|nv21|rgb24|rgb32] [qp <n>] [gop <n>] "
			"[nr] [noise] [raw <file>]\n");
		return 2;
	}

	meson_venc_start start = {};
	start.width = atoi(argv[2]);
	start.height = atoi(argv[3]);
	int frames = atoi(argv[4]);
	const char* outPath = argv[5];
	start.format = MESON_VENC_NV12;
	uint32 qp = 28;
	int gop = 30;
	const char* rawPath = NULL;
	for (int i = 6; i < argc; i++) {
		if (strcmp(argv[i], "nv12") == 0)
			start.format = MESON_VENC_NV12;
		else if (strcmp(argv[i], "nv21") == 0)
			start.format = MESON_VENC_NV21;
		else if (strcmp(argv[i], "rgb24") == 0)
			start.format = MESON_VENC_RGB24;
		else if (strcmp(argv[i], "rgb32") == 0)
			start.format = MESON_VENC_RGB32;
		else if (strcmp(argv[i], "nr") == 0)
			start.flags |= MESON_VENC_NOISE_REDUCTION;
		else if (strcmp(argv[i], "noise") == 0)
			sNoise = true;
		else if (strcmp(argv[i], "qp") == 0 && i + 1 < argc)
			qp = atoi(argv[++i]);
		else if (strcmp(argv[i], "gop") == 0 && i + 1 < argc)
			gop = atoi(argv[++i]);
		else if (strcmp(argv[i], "raw") == 0 && i + 1 < argc)
			rawPath = argv[++i];
	}

	static uint8 microcode[MESON_VENC_FIRMWARE_SIZE];
	FILE* file = fopen(argv[1], "rb");
	if (file == NULL || fread(microcode, 1, sizeof(microcode), file)
			!= sizeof(microcode)) {
		fprintf(stderr, "%s: need %d bytes\n", argv[1],
			MESON_VENC_FIRMWARE_SIZE);
		return 1;
	}
	fclose(file);
	start.firmware = microcode;
	start.firmware_size = sizeof(microcode);

	int device = open(MESON_VENC_DEVICE, O_RDWR);
	if (device < 0) {
		perror(MESON_VENC_DEVICE);
		return 1;
	}
	bigtime_t startTime = system_time();
	if (ioctl(device, MESON_VENC_START, &start, sizeof(start)) != 0) {
		perror("MESON_VENC_START");
		return 1;
	}
	printf("started in %.1f ms: input %zu bytes, %u bytes a row, chroma at "
		"%u\n", (system_time() - startTime) / 1000.0, start.input_size,
		start.bytes_per_row, start.chroma_offset);

	uint8* input;
	area_id clone = clone_area("venctest input", (void**)&input,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, start.input);
	if (clone < 0) {
		fprintf(stderr, "clone_area: %s\n", strerror(clone));
		return 1;
	}

	FILE* out = fopen(outPath, "wb");
	FILE* raw = rawPath != NULL ? fopen(rawPath, "wb") : NULL;
	size_t bufferSize = 1024 * 1024;
	uint8* buffer = (uint8*)malloc(bufferSize);

	meson_venc_output headers = {};
	headers.buffer = buffer;
	headers.buffer_size = bufferSize;
	if (ioctl(device, MESON_VENC_HEADERS, &headers, sizeof(headers)) != 0) {
		perror("MESON_VENC_HEADERS");
		return 1;
	}
	printf("headers: %zu bytes:", headers.size);
	for (size_t i = 0; i < headers.size && i < 48; i++)
		printf(" %02x", buffer[i]);
	printf("\n");
	fwrite(buffer, 1, headers.size, out);

	bigtime_t total = 0;
	size_t totalBytes = 0;
	bigtime_t worst = 0;
	for (int frame = 0; frame < frames; frame++) {
		fill_picture(input, start, start.format, frame);
		if (raw != NULL)
			fwrite(input, 1, start.input_size, raw);

		meson_venc_encode encode = {};
		encode.flags = frame % gop == 0 ? MESON_VENC_IDR : 0;
		encode.qp = qp;
		encode.timeout = 1000000;
		encode.output.buffer = buffer;
		encode.output.buffer_size = bufferSize;
		if (ioctl(device, MESON_VENC_ENCODE, &encode, sizeof(encode)) != 0) {
			perror("MESON_VENC_ENCODE");
			return 1;
		}
		fwrite(buffer, 1, encode.output.size, out);
		total += encode.encode_time;
		totalBytes += encode.output.size;
		if (encode.encode_time > worst)
			worst = encode.encode_time;
		if (frame < 3 || frame == frames - 1) {
			printf("picture %d: %s, %zu bytes, %.2f ms:", frame,
				encode.idr ? "IDR" : "P", encode.output.size,
				encode.encode_time / 1000.0);
			for (size_t i = 0; i < encode.output.size && i < 12; i++)
				printf(" %02x", buffer[i]);
			printf("\n");
		}
	}
	printf("%d pictures: %.2f ms each on average (worst %.2f), %.1f KiB "
		"each\n", frames, total / 1000.0 / frames, worst / 1000.0,
		totalBytes / 1024.0 / frames);

	fclose(out);
	if (raw != NULL)
		fclose(raw);
	ioctl(device, MESON_VENC_STOP, NULL, 0);
	close(device);
	return 0;
}
