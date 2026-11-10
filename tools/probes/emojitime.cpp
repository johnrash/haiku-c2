/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * emojitime: how long the app_server takes to draw strings with and without
 * emoji (milestone M56), into a B_RGBA32 bitmap: DrawString() and Sync(),
 * four times each; the first draw makes the glyphs (and the fallback
 * fonts' cache entries), the others take them from the glyph cache.
 * Prints the four times, in milliseconds, for each string.
 */


#include <stdio.h>
#include <string.h>

#include <Application.h>
#include <Bitmap.h>
#include <Font.h>
#include <View.h>


static void
time_string(BBitmap* bitmap, BView* view, const char* name, const char* text,
	float size)
{
	bitmap->Lock();
	BFont font(be_plain_font);
	font.SetSize(size);
	view->SetFont(&font);
	view->SetDrawingMode(B_OP_OVER);
	printf("%-28s %5.1f pt:", name, size);
	for (int pass = 0; pass < 4; pass++) {
		view->SetHighColor(255, 255, 255);
		view->FillRect(view->Bounds());
		view->SetHighColor(0, 0, 0);
		view->Sync();
		bigtime_t start = system_time();
		view->DrawString(text, BPoint(5, 100));
		view->Sync();
		printf(" %7.2f", (system_time() - start) / 1000.0);
	}
	printf(" ms\n");
	bitmap->Unlock();
}


int
main()
{
	BApplication app("application/x-vnd.c2-emojitime");
	BBitmap* bitmap = new BBitmap(BRect(0, 0, 799, 199),
		B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
	BView* view = new BView(bitmap->Bounds(), "view", B_FOLLOW_NONE, 0);
	bitmap->AddChild(view);

	char many[32 * 4 + 1];
	for (int i = 0; i < 32; i++) {
		unsigned c = 0x1f600 + i;
		many[i * 4] = 0xf0;
		many[i * 4 + 1] = 0x80 | (c >> 12 & 0x3f);
		many[i * 4 + 2] = 0x80 | (c >> 6 & 0x3f);
		many[i * 4 + 3] = 0x80 | (c & 0x3f);
	}
	many[128] = 0;
	char one[32 * 4 + 1];
	for (int i = 0; i < 32; i++)
		memcpy(one + i * 4, "\xf0\x9f\x98\x80", 4);
	one[128] = 0;
	const char* ascii = "abcdefghijklmnopqrstuvwxyzABCDEF";
	const char* stars = "\xe2\xad\x90\xe2\xad\x90\xe2\xad\x90\xe2\xad\x90"
		"\xe2\xad\x90\xe2\xad\x90\xe2\xad\x90\xe2\xad\x90";

	time_string(bitmap, view, "32 ASCII", ascii, 12);
	time_string(bitmap, view, "32 different emoji", many, 12.5);
	time_string(bitmap, view, "32 times one emoji", one, 13.5);
	time_string(bitmap, view, "8 stars (BMP)", stars, 14.5);
	time_string(bitmap, view, "32 different emoji", many, 48);
	return 0;
}
