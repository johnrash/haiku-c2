/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * overlaytest [<seconds> [anim [<fps>]]] [nv12]: shows a 1280x720
 * B_YCbCr422 (with "nv12" last: B_NV12) overlay bitmap, scaled into a
 * 960x540 window at 100,100: colour bars over a luma ramp, with a white
 * frame one pixel wide around the picture. "anim" cycles through three
 * bitmaps, as MediaPlayer does, with a bar moving across, at <fps> frames
 * per second (default: as fast as it can), and prints how many frames per
 * second it got. Exits 1 if the bitmap could not be an overlay.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <Bitmap.h>
#include <View.h>
#include <Window.h>


static const int kWidth = 1280;
static const int kHeight = 720;
static color_space sSpace = B_YCbCr422;


struct yuv {
	uint8 y, cb, cr;
};

// 75% bars, BT.601 limited range: white yellow cyan green magenta red
// blue black
static const yuv kBars[] = {
	{ 180, 128, 128 }, { 162, 44, 142 }, { 131, 156, 44 }, { 112, 72, 58 },
	{ 84, 184, 198 }, { 65, 100, 212 }, { 35, 212, 114 }, { 16, 128, 128 },
};


static yuv
pixel_pair(int x, int y, int barX, uint8* _y0, uint8* _y1)
{
	yuv color;
	if (y < kHeight * 2 / 3)
		color = kBars[x * 8 / kWidth];
	else {
		uint8 luma = 16 + x * 219 / kWidth;
		color = (yuv){ luma, 128, 128 };
	}
	if (barX >= 0 && x >= barX && x < barX + 32)
		color = (yuv){ 235, 128, 128 };
	uint8 y0 = color.y, y1 = color.y;
	if (y == 0 || y == kHeight - 1)
		y0 = y1 = 235;
	if (x == 0)
		y0 = 235;
	if (x == kWidth - 2)
		y1 = 235;
	*_y0 = y0;
	*_y1 = y1;
	return color;
}


static void
fill_frame(BBitmap* bitmap, int barX)
{
	uint8* bits = (uint8*)bitmap->Bits();
	int32 bytesPerRow = bitmap->BytesPerRow();
	for (int y = 0; y < kHeight; y++) {
		uint32* row = (uint32*)(bits + y * bytesPerRow);
		uint8* lumaRow = bits + y * bytesPerRow;
		// B_NV12: the CbCr plane after the Y plane, a pair for each 2x2
		uint8* chromaRow = bits + (kHeight + y / 2) * bytesPerRow;
		for (int x = 0; x < kWidth; x += 2) {
			uint8 y0, y1;
			yuv color = pixel_pair(x, y, barX, &y0, &y1);
			if (sSpace == B_NV12) {
				lumaRow[x] = y0;
				lumaRow[x + 1] = y1;
				if ((y & 1) == 0) {
					chromaRow[x] = color.cb;
					chromaRow[x + 1] = color.cr;
				}
				continue;
			}
			// B_YCbCr422 in memory: Y0 Cb Y1 Cr
			row[x / 2] = B_HOST_TO_LENDIAN_INT32(y0 | (color.cb << 8)
				| (y1 << 16) | ((uint32)color.cr << 24));
		}
	}
}


static BBitmap*
create_overlay_bitmap(bool reserveChannel)
{
	BBitmap* bitmap = new BBitmap(BRect(0, 0, kWidth - 1, kHeight - 1),
		B_BITMAP_WILL_OVERLAY
			| (reserveChannel ? B_BITMAP_RESERVE_OVERLAY_CHANNEL : 0),
		sSpace);
	if (bitmap->InitCheck() != B_OK) {
		printf("overlaytest: no overlay bitmap: %s\n",
			strerror(bitmap->InitCheck()));
		delete bitmap;
		return NULL;
	}
	return bitmap;
}


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 10;
	bool animate = argc > 2 && strcmp(argv[2], "anim") == 0;
	int fps = argc > 3 && strcmp(argv[3], "nv12") != 0 ? atoi(argv[3]) : 0;
	if (argc > 1 && strcmp(argv[argc - 1], "nv12") == 0)
		sSpace = B_NV12;
	const int kBitmapCount = 3;
	BApplication app("application/x-vnd.c2-overlaytest");

	BBitmap* bitmaps[kBitmapCount] = { create_overlay_bitmap(true) };
	if (bitmaps[0] == NULL)
		return 1;
	for (int i = 1; animate && i < kBitmapCount; i++) {
		if ((bitmaps[i] = create_overlay_bitmap(false)) == NULL)
			return 1;
	}
	printf("overlaytest: %s bitmap bits %p, %" B_PRId32 " bytes per row, %"
		B_PRId32 " bytes\n", sSpace == B_NV12 ? "B_NV12" : "B_YCbCr422",
		bitmaps[0]->Bits(), bitmaps[0]->BytesPerRow(),
		bitmaps[0]->BitsLength());

	if (bitmaps[0]->LockBits() != B_OK) {
		printf("overlaytest: LockBits failed\n");
		return 1;
	}
	fill_frame(bitmaps[0], -1);
	bitmaps[0]->UnlockBits();

	BWindow* window = new BWindow(BRect(100, 100, 1059, 639), "overlaytest",
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_QUIT_ON_WINDOW_CLOSE);
	BView* view = new BView(window->Bounds(), "video", B_FOLLOW_ALL,
		B_WILL_DRAW);
	window->AddChild(view);
	window->Show();

	window->Lock();
	rgb_color key;
	status_t status = view->SetViewOverlay(bitmaps[0], bitmaps[0]->Bounds(),
		view->Bounds(), &key, B_FOLLOW_ALL,
		B_OVERLAY_FILTER_HORIZONTAL | B_OVERLAY_FILTER_VERTICAL);
	if (status == B_OK) {
		view->SetLowColor(key);
		view->FillRect(view->Bounds(), B_SOLID_LOW);
		view->Sync();
	}
	window->Unlock();
	if (status != B_OK) {
		printf("overlaytest: SetViewOverlay: %s\n", strerror(status));
		return 1;
	}
	printf("overlaytest: overlay on, key %u,%u,%u,%u\n", key.red, key.green,
		key.blue, key.alpha);

	overlay_restrictions restrictions;
	if (bitmaps[0]->GetOverlayRestrictions(&restrictions) == B_OK) {
		printf("overlaytest: scale %.2f-%.2f x %.2f-%.2f\n",
			restrictions.min_width_scale, restrictions.max_width_scale,
			restrictions.min_height_scale, restrictions.max_height_scale);
	}

	if (!animate) {
		snooze(seconds * 1000000LL);
	} else {
		bigtime_t start = system_time();
		bigtime_t end = start + seconds * 1000000LL;
		int frames = 0;
		while (system_time() < end) {
			if (fps > 0) {
				snooze_until(start + (bigtime_t)frames * 1000000 / fps,
					B_SYSTEM_TIMEBASE);
			}
			BBitmap* bitmap = bitmaps[(frames + 1) % kBitmapCount];
			if (bitmap->LockBits() != B_OK)
				break;
			fill_frame(bitmap, (frames * 8) % (kWidth - 32));
			bitmap->UnlockBits();
			window->Lock();
			view->SetViewOverlay(bitmap, bitmap->Bounds(), view->Bounds(),
				&key, B_FOLLOW_ALL, B_OVERLAY_FILTER_HORIZONTAL
					| B_OVERLAY_FILTER_VERTICAL | B_OVERLAY_TRANSFER_CHANNEL);
			window->Unlock();
			frames++;
		}
		printf("overlaytest: %d frames, %.1f per second\n", frames,
			frames * 1000000.0 / (system_time() - start));
	}

	window->Lock();
	view->ClearViewOverlay();
	window->Quit();
	for (int i = 0; i < kBitmapCount; i++)
		delete bitmaps[i];
	return 0;
}
