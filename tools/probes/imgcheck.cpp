/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * imgcheck <image>...: which translator identifies each image, and the
 * colors BTranslationUtils::GetBitmap() decodes at the centers of its four
 * quadrants (test images: red, green, blue and white quadrants).
 */
#include <stdio.h>

#include <Application.h>
#include <Bitmap.h>
#include <File.h>
#include <TranslationUtils.h>
#include <TranslatorRoster.h>


int
main(int argc, char** argv)
{
	BApplication app("application/x-vnd.c2-imgcheck");
	BTranslatorRoster* roster = BTranslatorRoster::Default();
	for (int i = 1; i < argc; i++) {
		BFile file(argv[i], B_READ_ONLY);
		translator_info info;
		if (file.InitCheck() != B_OK
			|| roster->Identify(&file, NULL, &info) != B_OK) {
			printf("%s: not identified\n", argv[i]);
			continue;
		}
		BBitmap* bitmap = BTranslationUtils::GetBitmap(argv[i]);
		if (bitmap == NULL) {
			printf("%s: %s, no bitmap\n", argv[i], info.name);
			continue;
		}
		BRect b = bitmap->Bounds();
		int width = (int)b.Width() + 1, height = (int)b.Height() + 1;
		printf("%s: %s (%s), %d x %d, colorspace 0x%x:", argv[i], info.name,
			info.MIME, width, height, (unsigned)bitmap->ColorSpace());
		int bytesPerPixel = 0;
		if (bitmap->ColorSpace() == B_RGB32 || bitmap->ColorSpace() == B_RGBA32)
			bytesPerPixel = 4;
		else if (bitmap->ColorSpace() == B_RGB24)
			bytesPerPixel = 3;
		if (bytesPerPixel != 0) {
			const int xs[] = { width / 4, width * 3 / 4 };
			const int ys[] = { height / 4, height * 3 / 4 };
			for (int y = 0; y < 2; y++) {
				for (int x = 0; x < 2; x++) {
					const uint8* p = (const uint8*)bitmap->Bits()
						+ ys[y] * bitmap->BytesPerRow() + xs[x] * bytesPerPixel;
					printf(" %02x%02x%02x", p[2], p[1], p[0]);
				}
			}
		}
		printf("\n");
		delete bitmap;
	}
	return 0;
}
