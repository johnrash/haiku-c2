/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * opaquecopy [width height [rounds [offset]]]: app_server's copy of a
 * B_RGB32 bitmap into its back buffer (malloc'ed, B_RGBA32), made opaque,
 * in one process: a pixel at a time, four at a time (GCC's vector types),
 * four at a time with the destination aligned first, and memcpy then the
 * alpha in place; and plain memcpy. <offset>: the window's place in the
 * back buffer, in pixels. Prints the milliseconds a picture for each.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <OS.h>


typedef uint32 vector4 __attribute__((vector_size(16), aligned(4),
	may_alias));


static void
scalar(uint32* d, const uint32* s, int n)
{
	while (n--)
		*d++ = *s++ | 0xff000000;
}


static void
vectors(uint32* d, const uint32* s, int n)
{
	const vector4 opaque = { 0xff000000, 0xff000000, 0xff000000,
		0xff000000 };
	for (; n >= 16; n -= 16, s += 16, d += 16) {
		vector4 a = *(const vector4*)s | opaque;
		vector4 b = *(const vector4*)(s + 4) | opaque;
		vector4 c = *(const vector4*)(s + 8) | opaque;
		vector4 e = *(const vector4*)(s + 12) | opaque;
		*(vector4*)d = a;
		*(vector4*)(d + 4) = b;
		*(vector4*)(d + 8) = c;
		*(vector4*)(d + 12) = e;
	}
	while (n--)
		*d++ = *s++ | 0xff000000;
}


/*!	Four at a time with the destination aligned first: a window's place in
	the back buffer seldom is (stores across cache lines cost on the A53).
*/
static void
alignedVectors(uint32* d, const uint32* s, int n)
{
	while (n > 0 && ((addr_t)d & 15) != 0) {
		*d++ = *s++ | 0xff000000;
		n--;
	}
	vectors(d, s, n);
}


static void
copyThenAlpha(uint32* d, const uint32* s, int n)
{
	memcpy(d, s, n * 4);
	const vector4 opaque = { 0xff000000, 0xff000000, 0xff000000,
		0xff000000 };
	for (; n >= 4; n -= 4, d += 4)
		*(vector4*)d |= opaque;
	while (n--)
		*d++ |= 0xff000000;
}


static void
plain(uint32* d, const uint32* s, int n)
{
	memcpy(d, s, n * 4);
}


int
main(int argc, char** argv)
{
	int width = argc > 2 ? atoi(argv[1]) : 1900;
	int height = argc > 2 ? atoi(argv[2]) : 1000;
	int rounds = argc > 3 ? atoi(argv[3]) : 50;
	// the window's place in the back buffer, in pixels
	int offset = argc > 4 ? atoi(argv[4]) : 0;
	size_t size = (size_t)width * height * 4;
	uint32* source = (uint32*)malloc(size);
	uint32* target = (uint32*)malloc(size + 64);
	for (size_t i = 0; i < size / 4; i++)
		source[i] = i * 2654435761u;
	memset(target, 0, size);

	struct {
		const char* name;
		void (*row)(uint32*, const uint32*, int);
	} kWays[] = {
		{ "a pixel at a time", scalar },
		{ "four at a time", vectors },
		{ "four, aligned", alignedVectors },
		{ "memcpy, then alpha", copyThenAlpha },
		{ "memcpy only", plain },
	};
	for (int pass = 0; pass < 2; pass++) {
		for (size_t w = 0; w < sizeof(kWays) / sizeof(kWays[0]); w++) {
			bigtime_t start = system_time();
			for (int r = 0; r < rounds; r++) {
				for (int y = 0; y < height; y++) {
					kWays[w].row(target + offset + (size_t)y * width,
						source + (size_t)y * width, width);
				}
			}
			bigtime_t elapsed = system_time() - start;
			if (pass == 1) {
				printf("%-20s %.2f ms a %dx%d picture\n", kWays[w].name,
					elapsed / 1000.0 / rounds, width, height);
			}
		}
	}
	return 0;
}
