/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * scaletest [<rounds> [<width> <height>]]: app_server's bilinear bitmap
 * scaling, as MediaPlayer uses it for video: a 1280x720 B_RGB32 pattern drawn
 * to 1920x1080 (or <width> x <height>) in an
 * offscreen view with B_FILTER_BITMAP_BILINEAR. Prints the time per frame
 * and a checksum of the result's colour bytes (not the alpha byte, which
 * the x86 SIMD version writes and the C one does not), which must not
 * change with another implementation.
 */
#include <stdio.h>
#include <stdlib.h>

#include <Application.h>
#include <Bitmap.h>
#include <View.h>


int
main(int argc, char** argv)
{
	int rounds = argc > 1 ? atoi(argv[1]) : 20;
	int width = argc > 3 ? atoi(argv[2]) : 1920;
	int height = argc > 3 ? atoi(argv[3]) : 1080;
	BApplication app("application/x-vnd.c2-scaletest");

	BBitmap source(BRect(0, 0, 1279, 719), B_RGB32);
	uint8* bits = (uint8*)source.Bits();
	for (int y = 0; y < 720; y++) {
		uint8* row = bits + y * source.BytesPerRow();
		for (int x = 0; x < 1280; x++) {
			row[x * 4 + 0] = (x * 7 + y * 3) & 0xff;
			row[x * 4 + 1] = (x ^ y) & 0xff;
			row[x * 4 + 2] = ((x * y) >> 5) & 0xff;
			row[x * 4 + 3] = 255;
		}
	}

	BBitmap target(BRect(0, 0, width - 1, height - 1), B_RGB32, true);
	BView* view = new BView(target.Bounds(), "scaled", B_FOLLOW_NONE, 0);
	target.AddChild(view);

	bigtime_t best = B_INFINITE_TIMEOUT;
	for (int i = 0; i < rounds; i++) {
		target.Lock();
		bigtime_t start = system_time();
		view->DrawBitmap(&source, source.Bounds(), target.Bounds(),
			B_FILTER_BITMAP_BILINEAR);
		view->Sync();
		bigtime_t elapsed = system_time() - start;
		target.Unlock();
		if (elapsed < best)
			best = elapsed;
	}

	uint32 hash = 2166136261u;
	const uint8* result = (const uint8*)target.Bits();
	for (int y = 0; y < height; y++) {
		const uint8* row = result + y * target.BytesPerRow();
		for (int x = 0; x < width; x++) {
			for (int c = 0; c < 3; c++)
				hash = (hash ^ row[x * 4 + c]) * 16777619u;
		}
	}
	printf("scaletest: 1280x720 -> %dx%d bilinear: %.2f ms (best of %d), "
		"checksum %08x\n", width, height, best / 1000.0, rounds, (unsigned)hash);
	return 0;
}
