/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef LCD_FONT_H
#define LCD_FONT_H


#include <SupportDefs.h>


/*!	The display's character \a code as the panel draws it: eight rows of
	five dots (bit 4 the leftmost), the last row the cursor's, empty. Our
	own 5x7 letters in the manner of the module's: ASCII, and the codes
	outside it that the driver's table writes (the degree sign, arrows,
	some Greek and accented letters). A code without one gets an empty box.
*/
const uint8* lcd_glyph(uint8 code);


#endif	// LCD_FONT_H
