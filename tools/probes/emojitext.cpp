/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * emojitext [seconds]: colour emoji through the app_server (milestone M56).
 * Checks, drawing into a B_RGBA32 bitmap (white, black high colour) with
 * the plain font, which takes emoji from the "Noto Color Emoji" fallback:
 *   - the family is installed;
 *   - widths grow with the size;
 *   - B_OP_OVER and B_OP_COPY draw colours (a monochrome emoji has none),
 *     at sizes from 9 to 200;
 *   - B_OP_ALPHA with a high colour alpha of 96 draws them paler;
 *   - B_OP_INVERT draws the shape only (no colours);
 *   - the clipping region and ClipToPicture()'s alpha mask hold: nothing
 *     is drawn outside;
 *   - rotated text draws them too;
 *   - the look: U+2615 HOT BEVERAGE (an emoji by default, which the
 *     symbols font has too) in colour, but in black with U+FE0E after it;
 *     U+2764 HEAVY BLACK HEART (text by default) in black, but in colour
 *     with U+FE0F after it; U+263A WHITE SMILING FACE, which the plain font
 *     has, in colour with U+FE0F;
 *   - sequences drawn as one emoji (milestone M62): a flag, a ZWJ family, a
 *     skin tone, a keycap, ZWJ sequences with U+FE0F in them, a tag flag
 *     (England): each as wide as one emoji, and in colour (but the
 *     family, which Noto Color Emoji draws in greys).
 * Also prints how long 32 emoji take to draw the first time in a size
 * (each picture is scaled) and the second (from the glyph cache).
 * Prints each result; exit status 0 if all pass. Then shows a window with
 * emoji in text for <seconds> (default 0: none), for a look on the screen.
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <Bitmap.h>
#include <Font.h>
#include <Picture.h>
#include <Region.h>
#include <View.h>
#include <Window.h>


static const char* kEmoji = "\xf0\x9f\x98\x80";	// U+1F600 grinning face
static const int kWidth = 400;
static const int kHeight = 300;

static int sFailures = 0;


static void
check(bool passed, const char* what)
{
	printf("%s: %s\n", passed ? "pass" : "FAIL", what);
	if (!passed)
		sFailures++;
}


struct Census {
	int		coloured;	// clearly not grey
	int		touched;	// not white
	int		saturation;	// summed over the coloured ones
	BRect	bounds;		// of the touched pixels
};


static int
saturation(const uint8* p)
{
	int high = p[0] > p[1] ? p[0] : p[1];
	high = high > p[2] ? high : p[2];
	int low = p[0] < p[1] ? p[0] : p[1];
	low = low < p[2] ? low : p[2];
	return high - low;
}


/*!	Coloured: a pixel and its neighbours left and right are saturated and
	alike. Subpixel antialiasing colours the edges of black text, but only
	a pixel or two wide, and differently on each side of a stroke.
*/
static bool
coloured(const uint8* p)
{
	if (saturation(p - 4) <= 60 || saturation(p) <= 60
		|| saturation(p + 4) <= 60) {
		return false;
	}
	for (int c = 0; c < 3; c++) {
		if (abs(p[c - 4] - p[c]) > 40 || abs(p[c + 4] - p[c]) > 40)
			return false;
	}
	return true;
}


static Census
census(BBitmap* bitmap, BRect area)
{
	Census result = { 0, 0, 0, BRect() };
	const uint8* bits = (const uint8*)bitmap->Bits();
	int32 bpr = bitmap->BytesPerRow();
	int width = bitmap->Bounds().IntegerWidth() + 1;
	for (int y = (int)area.top; y <= (int)area.bottom; y++) {
		for (int x = (int)area.left; x <= (int)area.right; x++) {
			const uint8* p = bits + y * bpr + x * 4;	// B G R A
			if (p[0] < 250 || p[1] < 250 || p[2] < 250) {
				result.touched++;
				BRect pixel(x, y, x, y);
				result.bounds = result.bounds.IsValid()
					? result.bounds | pixel : pixel;
			}
			if (x > 0 && x < width - 1 && coloured(p)) {
				result.coloured++;
				result.saturation += saturation(p);
			}
		}
	}
	return result;
}


static void
clear(BView* view)
{
	view->SetDrawingMode(B_OP_COPY);
	view->SetHighColor(255, 255, 255);
	view->FillRect(view->Bounds());
	view->SetHighColor(0, 0, 0);
}


/*!	Draws the emoji at <size> with <mode> (and <alpha> as the high colour's
	alpha), returns the census of the whole bitmap.
*/
static Census
draw(BBitmap* bitmap, BView* view, float size, drawing_mode mode,
	uint8 alpha = 255, float rotation = 0, const char* text = kEmoji)
{
	bitmap->Lock();
	clear(view);
	BFont font(be_plain_font);
	font.SetSize(size);
	font.SetRotation(rotation);
	view->SetFont(&font);
	view->SetDrawingMode(mode);
	view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	view->SetHighColor(0, 0, 0, alpha);
	view->DrawString(text, BPoint(20, 20 + size));
	view->Sync();
	bitmap->Unlock();
	return census(bitmap, bitmap->Bounds());
}


static void
checkLook(BBitmap* bitmap, BView* view, const char* text, const char* name,
	bool colour)
{
	Census look = draw(bitmap, view, 48, B_OP_OVER, 255, 0, text);
	printf("  %s: %d pixels drawn, %d coloured\n", name, look.touched,
		look.coloured);
	char what[128];
	snprintf(what, sizeof(what), "%s in %s", name,
		colour ? "colour" : "black and white");
	// some emoji are mostly grey (U+2615's cup): any colour will do
	check(look.touched > 0 && (colour ? look.coloured >= 20
		: look.coloured == 0), what);
}


class EmojiView : public BView {
public:
	EmojiView(BRect frame)
		:
		BView(frame, "emoji", B_FOLLOW_ALL, B_WILL_DRAW)
	{
		SetViewColor(255, 255, 255);
	}

	void Draw(BRect)
	{
		static const float kSizes[] = { 12, 18, 24, 36, 48 };
		float y = 10;
		for (size_t i = 0; i < sizeof(kSizes) / sizeof(kSizes[0]); i++) {
			BFont font(be_plain_font);
			font.SetSize(kSizes[i]);
			SetFont(&font);
			y += kSizes[i] * 1.4f;
			char text[256];
			snprintf(text, sizeof(text), "%g pt: Haiku \xf0\x9f\x98\x80 "
				"\xf0\x9f\x91\x8d \xf0\x9f\x8e\x89 \xe2\x9d\xa4\xef\xb8\x8f "
				"\xe2\x98\x95 \xe2\xad\x90 \xf0\x9f\x9a\x80 \xf0\x9f\x8c\x88 "
				"\xf0\x9f\x90\xb1 \xe2\x9d\xa4 \xf0\x9f\x87\xaf\xf0\x9f\x87\xb5 "
				"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d"
				"\xf0\x9f\x91\xa7 \xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd 1\xef\xb8\x8f"
				"\xe2\x83\xa3 ok", kSizes[i]);
			SetHighColor(0, 0, 0);
			DrawString(text, BPoint(10, y));
		}
	}
};


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 0;
	BApplication app("application/x-vnd.c2-emojitext");

	// the family
	bool installed = false;
	int32 families = count_font_families();
	for (int32 i = 0; i < families; i++) {
		font_family family;
		if (get_font_family(i, &family) == B_OK
			&& strcmp(family, "Noto Color Emoji") == 0) {
			installed = true;
		}
	}
	check(installed, "\"Noto Color Emoji\" is installed");

	// widths
	BFont font(be_plain_font);
	float previous = 0;
	bool growing = true;
	static const float kSizes[] = { 9, 12, 18, 24, 36, 48, 72, 128, 200 };
	for (size_t i = 0; i < sizeof(kSizes) / sizeof(kSizes[0]); i++) {
		font.SetSize(kSizes[i]);
		float width = font.StringWidth(kEmoji);
		printf("  %g pt: width %.1f (%.2f em)\n", kSizes[i], width,
			width / kSizes[i]);
		if (width <= previous)
			growing = false;
		previous = width;
	}
	check(growing, "the emoji's width grows with the size");

	BBitmap* bitmap = new BBitmap(BRect(0, 0, kWidth - 1, kHeight - 1),
		B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
	BView* view = new BView(bitmap->Bounds(), "view", B_FOLLOW_NONE, 0);
	bitmap->AddChild(view);

	// colours, at every size
	int coloured48 = 0;
	int saturation48 = 0;
	for (size_t i = 0; i < sizeof(kSizes) / sizeof(kSizes[0]); i++) {
		Census over = draw(bitmap, view, kSizes[i], B_OP_OVER);
		printf("  %g pt, B_OP_OVER: %d pixels drawn, %d coloured, bounds "
			"%.0f,%.0f-%.0f,%.0f\n", kSizes[i], over.touched, over.coloured,
			over.bounds.left, over.bounds.top, over.bounds.right,
			over.bounds.bottom);
		char what[64];
		snprintf(what, sizeof(what), "%g pt: colours", kSizes[i]);
		check(over.coloured > over.touched / 8, what);
		if (kSizes[i] == 48) {
			coloured48 = over.coloured;
			saturation48 = over.saturation / (over.coloured > 0
				? over.coloured : 1);
		}
	}
	Census copy = draw(bitmap, view, 48, B_OP_COPY);
	check(copy.coloured > copy.touched / 8 && abs(copy.coloured - coloured48)
		< coloured48 / 10, "B_OP_COPY draws them as B_OP_OVER does");

	Census alpha = draw(bitmap, view, 48, B_OP_ALPHA, 96);
	int alphaSaturation = alpha.saturation / (alpha.coloured > 0
		? alpha.coloured : 1);
	printf("  B_OP_ALPHA 96: %d coloured, mean saturation %d (opaque %d)\n",
		alpha.coloured, alphaSaturation, saturation48);
	check(alpha.coloured > 0 && alphaSaturation < saturation48 * 3 / 4,
		"B_OP_ALPHA with alpha 96 draws them paler");

	Census invert = draw(bitmap, view, 48, B_OP_INVERT);
	printf("  B_OP_INVERT: %d pixels drawn, %d coloured\n", invert.touched,
		invert.coloured);
	check(invert.touched > 0 && invert.coloured == 0,
		"B_OP_INVERT draws the shape, without colours");

	// the clipping region: only the left half
	bitmap->Lock();
	clear(view);
	BRegion left(BRect(0, 0, 49, kHeight - 1));
	view->ConstrainClippingRegion(&left);
	font.SetSize(72);
	view->SetFont(&font);
	view->SetDrawingMode(B_OP_OVER);
	view->DrawString(kEmoji, BPoint(20, 100));
	view->Sync();
	view->ConstrainClippingRegion(NULL);
	bitmap->Unlock();
	Census inside = census(bitmap, BRect(0, 0, 49, kHeight - 1));
	Census outside = census(bitmap, BRect(50, 0, kWidth - 1, kHeight - 1));
	printf("  clipped to x < 50: %d pixels drawn inside, %d outside\n",
		inside.touched, outside.touched);
	check(inside.coloured > 0 && outside.touched == 0,
		"the clipping region holds");

	// ClipToPicture(): only inside a rectangle, through the alpha mask
	bitmap->Lock();
	clear(view);
	BPicture picture;
	view->BeginPicture(&picture);
	view->FillRect(BRect(0, 0, kWidth - 1, 69));
	view->EndPicture();
	view->PushState();
	view->ClipToPicture(&picture);
	view->SetHighColor(0, 0, 0);
	view->SetDrawingMode(B_OP_OVER);
	view->SetFont(&font);
	view->DrawString(kEmoji, BPoint(20, 100));
	view->Sync();
	view->PopState();
	bitmap->Unlock();
	Census above = census(bitmap, BRect(0, 0, kWidth - 1, 69));
	Census below = census(bitmap, BRect(0, 70, kWidth - 1, kHeight - 1));
	printf("  alpha mask to y < 70: %d pixels drawn inside, %d outside\n",
		above.touched, below.touched);
	check(above.coloured > 0 && below.touched == 0,
		"ClipToPicture()'s alpha mask holds");

	Census rotated = draw(bitmap, view, 48, B_OP_OVER, 255, 30);
	check(rotated.coloured > rotated.touched / 8,
		"rotated text draws them (upright)");

	checkLook(bitmap, view, "\xe2\x98\x95", "U+2615", true);
	checkLook(bitmap, view, "\xe2\x98\x95\xef\xb8\x8e", "U+2615 U+FE0E", false);
	checkLook(bitmap, view, "\xe2\x9d\xa4", "U+2764", false);
	checkLook(bitmap, view, "\xe2\x9d\xa4\xef\xb8\x8f", "U+2764 U+FE0F", true);
	checkLook(bitmap, view, "\xe2\x98\xba\xef\xb8\x8f", "U+263A U+FE0F", true);

	// sequences: one emoji's width each, in colour
	static const struct {
		const char* name;
		const char* text;
		bool colour;
	} kSequences[] = {
		{ "flag JP", "\xf0\x9f\x87\xaf\xf0\x9f\x87\xb5", true },
		{ "family (ZWJ)", "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9"
			"\xe2\x80\x8d\xf0\x9f\x91\xa7", false },
		{ "thumbs up, medium skin", "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd",
			true },
		{ "keycap 1", "1\xef\xb8\x8f\xe2\x83\xa3", true },
		{ "heart on fire (U+FE0F)", "\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d"
			"\xf0\x9f\x94\xa5", true },
		{ "rainbow flag (U+FE0F)", "\xf0\x9f\x8f\xb3\xef\xb8\x8f\xe2\x80\x8d"
			"\xf0\x9f\x8c\x88", true },
		{ "England (tags)", "\xf0\x9f\x8f\xb4\xf3\xa0\x81\xa7\xf3\xa0\x81\xa2"
			"\xf3\xa0\x81\xa5\xf3\xa0\x81\xae\xf3\xa0\x81\xa7\xf3\xa0\x81\xbf",
			true },
	};
	font.SetSize(48);
	float single = font.StringWidth(kEmoji);
	for (size_t i = 0; i < sizeof(kSequences) / sizeof(kSequences[0]); i++) {
		float width = font.StringWidth(kSequences[i].text);
		Census look = draw(bitmap, view, 48, B_OP_OVER, 255, 0,
			kSequences[i].text);
		printf("  %s: width %.0f (one emoji %.0f), %d pixels drawn, %d "
			"coloured\n", kSequences[i].name, width, single, look.touched,
			look.coloured);
		char what[128];
		snprintf(what, sizeof(what), "%s as one emoji", kSequences[i].name);
		check(fabsf(width - single) <= single / 4 && look.touched > 0
			&& (!kSequences[i].colour || look.coloured >= 20), what);
	}

	// U+1F600 to U+1F61F, in a size nothing drew yet
	char many[32 * 4 + 1];
	for (int i = 0; i < 32; i++) {
		uint32 c = 0x1f600 + i;
		many[i * 4] = 0xf0;
		many[i * 4 + 1] = 0x80 | (c >> 12 & 0x3f);
		many[i * 4 + 2] = 0x80 | (c >> 6 & 0x3f);
		many[i * 4 + 3] = 0x80 | (c & 0x3f);
	}
	many[32 * 4] = 0;
	bitmap->Lock();
	font.SetSize(11.5f);
	view->SetFont(&font);
	bigtime_t times[2];
	for (int pass = 0; pass < 2; pass++) {
		clear(view);
		view->SetDrawingMode(B_OP_OVER);
		bigtime_t start = system_time();
		view->DrawString(many, BPoint(5, 200));
		view->Sync();
		times[pass] = system_time() - start;
	}
	bitmap->Unlock();
	printf("  32 emoji at 11.5 pt: %.2f ms the first time, %.2f ms from the "
		"cache\n", times[0] / 1000.0, times[1] / 1000.0);

	delete bitmap;
	printf("%d checks failed\n", sFailures);

	if (seconds > 0) {
		BWindow* window = new BWindow(BRect(60, 80, 900, 480), "emojitext",
			B_TITLED_WINDOW, B_NOT_RESIZABLE);
		window->AddChild(new EmojiView(window->Bounds()));
		window->Show();
		snooze(seconds * 1000000LL);
		window->Lock();
		window->Quit();
	}
	return sFailures > 0 ? 1 : 0;
}
