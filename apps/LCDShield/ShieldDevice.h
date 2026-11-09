/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef SHIELD_DEVICE_H
#define SHIELD_DEVICE_H


#include <SupportDefs.h>

#include <lcd_shield.h>


/*!	The 16x2 LCD + IO Shield's devices (meson_gpio's /dev/misc/shield). */
class ShieldDevice {
public:
								ShieldDevice();
								~ShieldDevice();

			status_t			Open();
			void				Close();

			// a line of the display, padded or cut to 16 characters
			status_t			SetLine(uint32 row, const char* text);
			// a line as it is written: UTF-8, user characters as bytes 0
			// to 7 (0 too), 16 characters of them
			status_t			SetLineRaw(uint32 row, const char* bytes,
									size_t length);
			status_t			Clear();
			status_t			DefineCharacter(uint32 code,
									const uint8 rows[8]);
			status_t			GetText(lcd_shield_text& text);

			status_t			SetLeds(uint32 leds);
			uint32				Leds();

			uint32				Buttons();
			status_t			WaitButton(bigtime_t timeout,
									lcd_shield_button_event& event);

private:
			int					fLcd;
			int					fLeds;
			int					fButtons;
};


#endif	// SHIELD_DEVICE_H
