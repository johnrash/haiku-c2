/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * scalecheck [<width> <height>]: app_server's bilinear scaling on screen,
 * through clipping. A window shows a 1280x720 pattern scaled to <width> x
 * <height> (default 1600x900) with B_FILTER_BITMAP_BILINEAR while a
 * second window covers part of it, so app_server scales one clipping
 * rectangle at a time; some redraws are of small invalidated areas. The
 * screen (BScreen::GetBitmap) is then compared with the same scaling done
 * offscreen, outside the covering window, with the mouse pointer hidden
 * (the window starts below the top 100 lines, where notifications show).
 * Prints the number of differing pixels (colour bytes only) and exits 1 if
 * there are any.
 */
#include <stdio.h>
#include <stdlib.h>

#include <Application.h>
#include <Bitmap.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>


static BBitmap* sSource;


class ScaledView : public BView {
public:
	ScaledView(BRect frame)
		:
		BView(frame, "scaled", B_FOLLOW_NONE, B_WILL_DRAW)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	virtual void Draw(BRect updateRect)
	{
		DrawBitmap(sSource, sSource->Bounds(), Bounds(),
			B_FILTER_BITMAP_BILINEAR);
	}
};


int
main(int argc, char** argv)
{
	int width = argc > 2 ? atoi(argv[1]) : 1600;
	int height = argc > 2 ? atoi(argv[2]) : 900;
	BApplication app("application/x-vnd.c2-scalecheck");

	sSource = new BBitmap(BRect(0, 0, 1279, 719), B_RGB32);
	uint8* bits = (uint8*)sSource->Bits();
	for (int y = 0; y < 720; y++) {
		uint8* row = bits + y * sSource->BytesPerRow();
		for (int x = 0; x < 1280; x++) {
			row[x * 4 + 0] = (x * 7 + y * 3) & 0xff;
			row[x * 4 + 1] = (x ^ y) & 0xff;
			row[x * 4 + 2] = ((x * y) >> 5) & 0xff;
			row[x * 4 + 3] = 255;
		}
	}

	// the reference, scaled offscreen without clipping
	BBitmap reference(BRect(0, 0, width - 1, height - 1), B_RGB32, true);
	BView* referenceView = new BView(reference.Bounds(), "reference",
		B_FOLLOW_NONE, 0);
	reference.AddChild(referenceView);
	reference.Lock();
	referenceView->DrawBitmap(sSource, sSource->Bounds(),
		reference.Bounds(), B_FILTER_BITMAP_BILINEAR);
	referenceView->Sync();
	reference.Unlock();

	BRect frame(60, 100, 60 + width - 1, 100 + height - 1);
	BWindow* window = new BWindow(frame, "scalecheck", B_NO_BORDER_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL, B_NOT_MOVABLE | B_NOT_RESIZABLE);
	ScaledView* view = new ScaledView(window->Bounds());
	window->AddChild(view);
	window->Show();

	// covers part of the scaled window: its visible region is four
	// rectangles
	BRect coverFrame(frame.left + width / 3, frame.top + height / 3,
		frame.left + width / 3 + 299, frame.top + height / 3 + 199);
	BWindow* cover = new BWindow(coverFrame, "cover", B_NO_BORDER_WINDOW_LOOK,
		B_FLOATING_ALL_WINDOW_FEEL, B_NOT_MOVABLE | B_NOT_RESIZABLE);
	BView* coverView = new BView(cover->Bounds(), "cover", B_FOLLOW_ALL, 0);
	coverView->SetViewColor(255, 0, 255);
	cover->AddChild(coverView);
	cover->Show();
	snooze(500000);

	// redraw small parts too: clipping rectangles of a few pixels
	window->Lock();
	view->Invalidate();
	for (int i = 0; i < 40; i++) {
		BRect area(i * 37 % width, i * 23 % height, 0, 0);
		area.right = area.left + i % 7;
		area.bottom = area.top + i % 5;
		view->Invalidate(area);
	}
	view->Sync();
	window->Unlock();
	snooze(500000);

	be_app->HideCursor();
	snooze(200000);
	BBitmap* screen = NULL;
	if (BScreen().GetBitmap(&screen, false) != B_OK || screen == NULL) {
		printf("scalecheck: cannot read the screen\n");
		return 1;
	}
	int compared = 0, differing = 0;
	for (int y = 0; y < height; y++) {
		int screenY = (int)frame.top + y;
		if (screenY > screen->Bounds().bottom)
			break;
		const uint8* screenRow = (const uint8*)screen->Bits()
			+ screenY * screen->BytesPerRow();
		const uint8* referenceRow = (const uint8*)reference.Bits()
			+ y * reference.BytesPerRow();
		for (int x = 0; x < width; x++) {
			int screenX = (int)frame.left + x;
			if (screenX > screen->Bounds().right)
				break;
			if (coverFrame.Contains(BPoint(screenX, screenY)))
				continue;
			compared++;
			const uint8* s = screenRow + screenX * 4;
			const uint8* r = referenceRow + x * 4;
			if (s[0] != r[0] || s[1] != r[1] || s[2] != r[2]) {
				if (differing < 5) {
					printf("scalecheck: %d,%d: screen %02x%02x%02x, "
						"reference %02x%02x%02x\n", x, y, s[2], s[1], s[0],
						r[2], r[1], r[0]);
				}
				differing++;
			}
		}
	}
	be_app->ShowCursor();
	printf("scalecheck: %dx%d, %d pixels compared, %d differ\n", width,
		height, compared, differing);
	delete screen;
	cover->Lock();
	cover->Quit();
	window->Lock();
	window->Quit();
	return differing == 0 ? 0 : 1;
}
