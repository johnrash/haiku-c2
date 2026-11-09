/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * glpresent [frames] [rounds] [single]: what an OpenGL view shows, read
 * back from the screen (BScreen::GetBitmap) after it stops drawing: frames
 * of a known picture (a colour per frame, a white square at a place per
 * frame) drawn as fast as possible, then nothing more. The screen must show
 * the last frame, whole: the presenter may not lose or hold back the one
 * drawn last. Each round ends with a different frame count. Prints what it
 * found. "single": a single-buffered view (every frame into the same
 * buffer).
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <Bitmap.h>
#include <GLView.h>
#include <Screen.h>
#include <Window.h>

#include <GL/gl.h>


static const int kSize = 256;


static void
frame_colour(int frame, uint8* rgb)
{
	rgb[0] = (frame * 37) & 0xff;
	rgb[1] = (frame * 91 + 40) & 0xff;
	rgb[2] = (frame * 53 + 80) & 0xff;
}


int
main(int argc, char** argv)
{
	int frames = argc > 1 ? atoi(argv[1]) : 100;
	int rounds = argc > 2 ? atoi(argv[2]) : 5;
	bool single = argc > 3 && strcmp(argv[3], "single") == 0;
	BApplication app("application/x-vnd.c2-glpresent");

	BRect frame(100, 100, 100 + kSize - 1, 100 + kSize - 1);
	BWindow* window = new BWindow(frame, "glpresent", B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE);
	BGLView* view = new BGLView(window->Bounds(), "gl", B_FOLLOW_ALL_SIDES,
		0, BGL_RGB | (single ? BGL_SINGLE : BGL_DOUBLE));
	window->AddChild(view);
	window->Show();
	snooze(500000);

	int failures = 0;
	for (int round = 0; round < rounds; round++) {
		int count = frames + round * 7;
		int last = 0;
		view->LockGL();
		for (int i = 0; i < count; i++) {
			uint8 rgb[3];
			frame_colour(i, rgb);
			glClearColor(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f,
				1);
			glClear(GL_COLOR_BUFFER_BIT);
			// a white square, its place moving with the frame
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glOrtho(0, kSize, 0, kSize, -1, 1);
			glColor3f(1, 1, 1);
			int x = (i * 13) % (kSize - 32);
			glBegin(GL_QUADS);
			glVertex2i(x, 100);
			glVertex2i(x + 32, 100);
			glVertex2i(x + 32, 132);
			glVertex2i(x, 132);
			glEnd();
			view->SwapBuffers();
			last = i;
		}
		view->UnlockGL();
		// nothing more is drawn: what shows after a while is what stays
		snooze(300000);

		BScreen screen(window);
		BBitmap* shot = NULL;
		window->Lock();
		BRect onScreen = view->ConvertToScreen(view->Bounds());
		window->Unlock();
		if (screen.GetBitmap(&shot, false, &onScreen) != B_OK || shot == NULL) {
			printf("round %d: no screen bitmap\n", round);
			failures++;
			continue;
		}
		uint8 expected[3];
		frame_colour(last, expected);
		int squareX = (last * 13) % (kSize - 32);
		const uint8* bits = (const uint8*)shot->Bits();
		int32 bpr = shot->BytesPerRow();
		int wrong = 0;
		int squareWrong = 0;
		for (int y = 0; y < kSize; y++) {
			for (int x = 0; x < kSize; x++) {
				const uint8* p = bits + y * bpr + x * 4;	// B G R A
				// GL's y is bottom up
				int glY = kSize - 1 - y;
				bool inSquare = x >= squareX && x < squareX + 32
					&& glY >= 100 && glY < 132;
				// leave the square's edges out (rasterisation)
				bool edge = (x >= squareX - 1 && x <= squareX
						|| x >= squareX + 31 && x <= squareX + 32)
					&& glY >= 99 && glY <= 132
					|| (glY >= 99 && glY <= 100 || glY >= 131 && glY <= 132)
						&& x >= squareX - 1 && x <= squareX + 32;
				if (edge)
					continue;
				if (inSquare) {
					if (p[0] < 250 || p[1] < 250 || p[2] < 250)
						squareWrong++;
				} else if (abs(p[2] - expected[0]) > 2
					|| abs(p[1] - expected[1]) > 2
					|| abs(p[0] - expected[2]) > 2) {
					wrong++;
				}
			}
		}
		const uint8* centre = bits + (kSize / 2) * bpr + 8 * 4;
		printf("round %d: %d frames; shown: %d pixels not frame %d's colour "
			"(%u %u %u, the screen %u %u %u), %d of its square wrong\n",
			round, count, wrong, last, expected[0], expected[1], expected[2],
			centre[2], centre[1], centre[0], squareWrong);
		if (wrong > 0 || squareWrong > 0)
			failures++;
		delete shot;
	}
	printf("%d of %d rounds wrong\n", failures, rounds);

	window->Lock();
	window->Quit();
	return failures > 0 ? 1 : 0;
}
