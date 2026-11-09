/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * pwmprobe: steps PWM E (header pin 11) through a sequence of periods and
 * pulses with meson_gpio's ioctls, and after each step prints the EF
 * controller's registers (read through /dev/misc/mem) and the pin's high
 * share and rising edges a second, sampled at random times.
 *
 *   pwmprobe <period ns>:<high ns> | m | w<ms> ...
 *
 * A period of 0 means 1 ms; a high time of 0 holds the pin low. "m"
 * measures again, "w<ms>" waits. Written to find how the S905's PWM takes
 * new counts (meson_gpio's _SetPwm()).
 *
 * Build: $CXX -O2 -I<haiku>/headers/private/drivers -o pwmprobe pwmprobe.cpp
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>

#include <meson_gpio.h>


static void
print_registers(const char* when)
{
	uint32 registers[4] = {};
	int fd = open("/dev/misc/mem", O_RDONLY);
	if (fd >= 0) {
		pread(fd, registers, sizeof(registers), 0xc11086c0);
		close(fd);
	}
	printf("  %-10s E %08x  F %08x  MISC %08x\n", when, registers[0],
		registers[1], registers[2]);
}


static void
measure(int fd, uint32 index)
{
	meson_gpio_pin_info info;
	info.index = index;
	int64 samples = 0, highs = 0, rises = 0;
	bool last = false;
	bigtime_t end = system_time() + 1000000;
	while (system_time() < end) {
		for (volatile int wait = rand() % 16; wait > 0; wait--)
			;
		ioctl(fd, MESON_GPIO_GET_PIN_INFO, &info, sizeof(info));
		bool high = (info.flags & MESON_GPIO_PIN_LEVEL) != 0;
		if (samples > 0 && high && !last)
			rises++;
		last = high;
		highs += high;
		samples++;
	}
	printf("  measured   %.2f %% high, %lld rises a second\n",
		highs * 100.0 / samples, (long long)rises);
}


int
main(int argc, char** argv)
{
	int fd = open(MESON_GPIO_DEVICE, O_RDWR);
	int infoFd = open(MESON_GPIO_DEVICE, O_RDONLY);
	meson_gpio_pins pins;
	pins.count = 1;
	pins.pins[0] = 11;
	if (fd < 0 || infoFd < 0
		|| ioctl(fd, MESON_GPIO_CLAIM, &pins, sizeof(pins)) != 0) {
		fprintf(stderr, "pwmprobe: pin 11 not available\n");
		return 1;
	}
	uint32 index = 0;
	meson_gpio_pin_info info;
	for (info.index = 0; ioctl(infoFd, MESON_GPIO_GET_PIN_INFO, &info,
			sizeof(info)) == 0 && info.pin != 11; info.index++)
		;
	index = info.index;

	print_registers("before");
	for (int i = 1; i < argc; i++) {
		const char* step = argv[i];
		if (step[0] == 'm') {
			printf("measure:\n");
			print_registers("now");
			measure(infoFd, index);
			continue;
		}
		if (step[0] == 'w') {
			snooze(atoi(step + 1) * 1000LL);
			continue;
		}
		meson_gpio_pwm_period period;
		period.bit = 0;
		period.period = strtoul(argv[i], NULL, 10);
		const char* colon = strchr(argv[i], ':');
		period.high = colon != NULL ? strtoul(colon + 1, NULL, 10) : 0;
		if (period.period == 0)
			period.period = 1000000;
		status_t status = ioctl(fd, MESON_GPIO_SET_PWM_PERIOD, &period,
			sizeof(period)) == 0 ? B_OK : errno;
		printf("%s: %s, period %u, high %u\n", argv[i], strerror(status),
			period.period, period.high);
		print_registers("at once");
		snooze(100000);
		print_registers("0.1 s");
		measure(infoFd, index);
		print_registers("1.1 s");
	}
	close(fd);
	close(infoFd);
	return 0;
}
