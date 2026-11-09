/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * drawbitmap <width> <height> [seconds] [rgba]: how long app_server takes to
 * draw a whole B_RGB32 (or B_RGBA32) bitmap 1:1 into a window of that size,
 * as an OpenGL view's frames are shown: DrawBitmap() and Sync() in a loop,
 * from a thread of its own. Prints the draws a second and the time each.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <Bitmap.h>
#include <View.h>
#include <Window.h>


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: drawbitmap <width> <height> [seconds] "
			"[rgba]\n");
		return 1;
	}
	int width = atoi(argv[1]);
	int height = atoi(argv[2]);
	int seconds = argc > 3 ? atoi(argv[3]) : 5;
	bool alpha = argc > 4 && strcmp(argv[4], "rgba") == 0;
	BApplication app("application/x-vnd.c2-drawbitmap");

	BWindow* window = new BWindow(BRect(5, 25, 5 + width - 1, 25 + height - 1),
		"drawbitmap", B_TITLED_WINDOW, B_NOT_RESIZABLE);
	BView* view = new BView(window->Bounds(), "view", B_FOLLOW_ALL_SIDES, 0);
	view->SetViewColor(B_TRANSPARENT_COLOR);
	window->AddChild(view);
	window->Show();
	snooze(500000);

	BBitmap* bitmaps[2];
	for (int i = 0; i < 2; i++) {
		bitmaps[i] = new BBitmap(BRect(0, 0, width - 1, height - 1),
			alpha ? B_RGBA32 : B_RGB32);
		uint32* bits = (uint32*)bitmaps[i]->Bits();
		for (int p = 0; p < width * height; p++)
			bits[p] = 0xff000000 | (i == 0 ? 0x336699 : 0x996633)
				| (p % width) & 0xff;
	}

	bigtime_t start = system_time();
	bigtime_t end = start + seconds * 1000000LL;
	int draws = 0;
	while (system_time() < end) {
		window->Lock();
		view->DrawBitmap(bitmaps[draws & 1], B_ORIGIN);
		view->Sync();
		window->Unlock();
		draws++;
	}
	bigtime_t elapsed = system_time() - start;
	printf("%dx%d %s: %d draws in %.1f s: %.1f a second, %.2f ms each\n",
		width, height, alpha ? "B_RGBA32" : "B_RGB32", draws, elapsed / 1e6,
		draws * 1e6 / elapsed, elapsed / 1e3 / draws);

	window->Lock();
	window->Quit();
	return 0;
}
