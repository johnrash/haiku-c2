/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * Tries Hardkernel's 16x2 LCD + IO Shield through /dev/misc/gpio from a
 * program, before its driver (milestone M68 (docs/milestones.md)): the pins as the shield's
 * schematic has them.
 *
 *   lcdshield lcd <line1> [<line2>]   the HD44780 started in 4-bit mode,
 *                                     the two lines written
 *   lcdshield leds <0101010>          LEDs D1 to D7 (pins 29, 31, 33, 35,
 *                                     36, 32, 26)
 *   lcdshield buttons <seconds>       SW1 (pin 18) and SW2 (pin 22), with
 *                                     pull-ups: their levels while they last
 *
 * The LCD's R/W is tied low: it is only written, with delays instead of
 * its busy flag (37 us a command, 1.52 ms to clear).
 *
 * Build: $CXX -O2 -I<haiku>/headers/private/drivers -o lcdshield
 *            lcdshield.cpp
 */


#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>

#include <meson_gpio.h>


// the LCD: RS, E, D4, D5, D6, D7
static const uint8 kLcdPins[] = { 7, 11, 13, 15, 12, 16 };
enum { RS = 1 << 0, E = 1 << 1, D4 = 2 };
static const uint8 kLedPins[] = { 29, 31, 33, 35, 36, 32, 26 };
static const uint8 kButtonPins[] = { 18, 22 };

static int sFd = -1;
static uint32 sLevels = 0;


static int
claim(const uint8* pins, uint32 count)
{
	sFd = open(MESON_GPIO_DEVICE, O_RDWR);
	if (sFd < 0) {
		perror(MESON_GPIO_DEVICE);
		return -1;
	}
	meson_gpio_pins claim;
	claim.count = count;
	memcpy(claim.pins, pins, count);
	if (ioctl(sFd, MESON_GPIO_CLAIM, &claim, sizeof(claim)) != 0) {
		perror("claim");
		return -1;
	}
	return 0;
}


static void
write_levels(uint32 levels)
{
	sLevels = levels;
	ioctl(sFd, MESON_GPIO_WRITE, &levels, sizeof(levels));
}


static void
nibble(uint8 value, bool data)
{
	uint32 levels = (data ? RS : 0) | ((uint32)(value & 0xf) << D4);
	write_levels(levels);
	write_levels(levels | E);
	snooze(1);
	write_levels(levels);
}


static void
lcd_byte(uint8 value, bool data, bigtime_t wait = 50)
{
	nibble(value >> 4, data);
	nibble(value, data);
	snooze(wait);
}


static int
lcd(const char* line1, const char* line2)
{
	if (claim(kLcdPins, sizeof(kLcdPins)) != 0)
		return 1;
	write_levels(0);
	uint32 outputs = (1 << sizeof(kLcdPins)) - 1;
	ioctl(sFd, MESON_GPIO_SET_OUTPUTS, &outputs, sizeof(outputs));
	snooze(50000);

	// the datasheet's initialization by instruction, into 4-bit mode
	nibble(0x3, false);
	snooze(4500);
	nibble(0x3, false);
	snooze(150);
	nibble(0x3, false);
	snooze(150);
	nibble(0x2, false);
	snooze(150);
	lcd_byte(0x28, false);		// 4-bit, 2 lines, 5x8
	lcd_byte(0x08, false);		// display off
	lcd_byte(0x01, false, 2000);	// clear
	lcd_byte(0x06, false);		// entry: increment, no shift
	lcd_byte(0x0c, false);		// display on, no cursor

	const char* lines[] = { line1, line2 };
	for (int row = 0; row < 2; row++) {
		if (lines[row] == NULL)
			continue;
		lcd_byte(0x80 | (row == 0 ? 0x00 : 0x40), false);
		for (int i = 0; i < 16 && lines[row][i] != '\0'; i++)
			lcd_byte((uint8)lines[row][i], true);
	}
	printf("lcd: written\n");
	return 0;
}


static int
leds(const char* pattern)
{
	if (claim(kLedPins, sizeof(kLedPins)) != 0)
		return 1;
	uint32 levels = 0;
	for (uint32 i = 0; i < sizeof(kLedPins) && pattern[i] != '\0'; i++) {
		if (pattern[i] == '1')
			levels |= 1 << i;
	}
	write_levels(levels);
	uint32 outputs = (1 << sizeof(kLedPins)) - 1;
	ioctl(sFd, MESON_GPIO_SET_OUTPUTS, &outputs, sizeof(outputs));
	printf("leds: %s\n", pattern);
	// the pins stay outputs only while the file is open
	sleep(30);
	return 0;
}


static int
buttons(int seconds)
{
	if (claim(kButtonPins, sizeof(kButtonPins)) != 0)
		return 1;
	meson_gpio_pull pull = { 3, MESON_GPIO_PULL_UP };
	ioctl(sFd, MESON_GPIO_SET_PULL, &pull, sizeof(pull));
	uint32 last = 0xffffffff;
	bigtime_t end = system_time() + seconds * 1000000LL;
	while (system_time() < end) {
		uint32 levels = 0;
		ioctl(sFd, MESON_GPIO_READ, &levels, sizeof(levels));
		if (levels != last) {
			printf("%.1f s: SW1 %s, SW2 %s\n",
				(seconds * 1000000LL - (end - system_time())) / 1e6,
				(levels & 1) ? "up" : "DOWN", (levels & 2) ? "up" : "DOWN");
			fflush(stdout);
			last = levels;
		}
		snooze(10000);
	}
	return 0;
}


int
main(int argc, char** argv)
{
	if (argc >= 3 && strcmp(argv[1], "lcd") == 0)
		return lcd(argv[2], argc > 3 ? argv[3] : NULL);
	if (argc >= 3 && strcmp(argv[1], "leds") == 0)
		return leds(argv[2]);
	if (argc >= 3 && strcmp(argv[1], "buttons") == 0)
		return buttons(atoi(argv[2]));
	fprintf(stderr, "usage: lcdshield lcd <line1> [<line2>] | leds <0101010>"
		" | buttons <seconds>\n");
	return 2;
}
