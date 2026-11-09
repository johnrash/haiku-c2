/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */


#include "ShieldDevice.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>


ShieldDevice::ShieldDevice()
	:
	fLcd(-1),
	fLeds(-1),
	fButtons(-1)
{
}


ShieldDevice::~ShieldDevice()
{
	Close();
}


status_t
ShieldDevice::Open()
{
	Close();
	fLcd = open(LCD_SHIELD_LCD_DEVICE, O_RDWR);
	fLeds = open(LCD_SHIELD_LEDS_DEVICE, O_RDWR);
	fButtons = open(LCD_SHIELD_BUTTONS_DEVICE, O_RDONLY);
	if (fLcd < 0 || fLeds < 0 || fButtons < 0) {
		status_t status = errno;
		Close();
		return status;
	}
	return B_OK;
}


void
ShieldDevice::Close()
{
	if (fLcd >= 0)
		close(fLcd);
	if (fLeds >= 0)
		close(fLeds);
	if (fButtons >= 0)
		close(fButtons);
	fLcd = fLeds = fButtons = -1;
}


status_t
ShieldDevice::SetLine(uint32 row, const char* text)
{
	// 16 characters (UTF-8 sequences count as one), then spaces
	char line[LCD_SHIELD_COLUMNS * 4 + 1];
	size_t length = 0;
	uint32 characters = 0;
	for (const char* c = text; *c != '\0' && characters < LCD_SHIELD_COLUMNS;
			) {
		size_t bytes = 1;
		uint8 first = (uint8)*c;
		if (first >= 0xf0)
			bytes = 4;
		else if (first >= 0xe0)
			bytes = 3;
		else if (first >= 0xc0)
			bytes = 2;
		for (size_t i = 0; i < bytes && *c != '\0'; i++)
			line[length++] = *c++;
		characters++;
	}
	while (characters++ < LCD_SHIELD_COLUMNS)
		line[length++] = ' ';

	return SetLineRaw(row, line, length);
}


status_t
ShieldDevice::SetLineRaw(uint32 row, const char* bytes, size_t length)
{
	lcd_shield_position position = { row, 0 };
	if (ioctl(fLcd, LCD_SHIELD_SET_POSITION, &position, sizeof(position))
			!= 0) {
		return errno;
	}
	return write(fLcd, bytes, length) == (ssize_t)length ? B_OK : errno;
}


status_t
ShieldDevice::Clear()
{
	return ioctl(fLcd, LCD_SHIELD_CLEAR, NULL, 0) == 0 ? B_OK : errno;
}


status_t
ShieldDevice::DefineCharacter(uint32 code, const uint8 rows[8])
{
	lcd_shield_character character;
	character.code = code;
	memcpy(character.rows, rows, sizeof(character.rows));
	return ioctl(fLcd, LCD_SHIELD_DEFINE_CHARACTER, &character,
		sizeof(character)) == 0 ? B_OK : errno;
}


status_t
ShieldDevice::GetText(lcd_shield_text& text)
{
	return ioctl(fLcd, LCD_SHIELD_GET_TEXT, &text, sizeof(text)) == 0
		? B_OK : errno;
}


status_t
ShieldDevice::SetLeds(uint32 leds)
{
	return ioctl(fLeds, LCD_SHIELD_SET_LEDS, &leds, sizeof(leds)) == 0
		? B_OK : errno;
}


uint32
ShieldDevice::Leds()
{
	uint32 leds = 0;
	ioctl(fLeds, LCD_SHIELD_GET_LEDS, &leds, sizeof(leds));
	return leds;
}


uint32
ShieldDevice::Buttons()
{
	uint32 down = 0;
	ioctl(fButtons, LCD_SHIELD_GET_BUTTONS, &down, sizeof(down));
	return down;
}


status_t
ShieldDevice::WaitButton(bigtime_t timeout, lcd_shield_button_event& event)
{
	event.timeout = timeout;
	return ioctl(fButtons, LCD_SHIELD_WAIT_BUTTON, &event, sizeof(event)) == 0
		? B_OK : errno;
}
