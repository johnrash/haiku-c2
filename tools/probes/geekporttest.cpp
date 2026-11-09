/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * Checks libdevice's GeekPort classes on the ODROID-C2 (milestone M27 (docs/milestones.md))
 * without anything wired to the header:
 *
 *   geekporttest digital    BDigitalPort: the Be Book's rules (opens as
 *                           input, Write() fails on an input, one owner),
 *                           then each port as an output, every pattern
 *                           written read back from the pins, and how many
 *                           writes and reads a second
 *   geekporttest adc        the converter's internal references (ground to
 *                           1.8 V in quarters) against what they should
 *                           read, then BA2D on the two inputs
 *   geekporttest pwm        BD2A's rules (Read() gives the value last
 *                           written, the output stays after Close()), then
 *                           each duty cycle measured by sampling the pin
 *   geekporttest pin        BDigitalPin: names, one owner, the pulls (an
 *                           open input follows its pull), outputs read back
 *   geekporttest bpwm       BPWM: stopped is low, duty cycles and
 *                           frequencies (50 Hz, 1 kHz) measured on the pin
 *   geekporttest geekport   BGeekPort: the 40 pins, and who has them
 *   geekporttest edge       BDigitalPin's edges, from an output's own:
 *                           timeouts, which edges, no edge from starting,
 *                           the order, a full queue, StartWatching()'s
 *                           messages, the 8 channels
 *   geekporttest serial     BSerialPort "header" (UART A, pins 8 and 10):
 *                           one owner, the pins taken and given back, the
 *                           frames sent (8N1, 8E1, 7O2) decoded from pin
 *                           8's samples, the speeds measured from long
 *                           writes (9600 bits/s to 4 Mbit/s), a break,
 *                           and bytes received: pin 10 pulled down and up
 *                           in turn, one bit after the other (through
 *                           /dev/misc/mem: root only, and only while
 *                           nothing else changes GPIOX pulls; UART A's mux
 *                           keeps the pad from being driven as GPIO, and
 *                           its "invert RX" bit does nothing)
 *   geekporttest i2c        BI2CDevice on the "header" bus (I2C B, pins
 *                           27 and 28) with nothing on it: names, the pins
 *                           taken and given back, no answers (quickly), a
 *                           scan, SCL clocking on pin 28
 *
 * Prints each check and exits 1 if one failed.
 *
 * Build: $CXX -O2 -I<haiku>/headers/private/drivers -o geekporttest \
 *            geekporttest.cpp -ldevice -lbe
 */


#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <A2D.h>
#include <D2A.h>
#include <DigitalPin.h>
#include <DigitalPort.h>
#include <GeekPort.h>
#include <I2CDevice.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <PWM.h>
#include <SerialPort.h>

#include <meson_gpio.h>
#include <meson_saradc.h>


static int sFailures = 0;


static void
check(bool good, const char* what)
{
	printf("%s  %s\n", good ? "ok  " : "FAIL", what);
	if (!good)
		sFailures++;
}


/*!	Whether the text interface refuses \a command (the pin is claimed). */
static bool
text_refused(const char* command)
{
	int fd = open("/dev/misc/gpio", O_WRONLY);
	if (fd < 0)
		return false;
	ssize_t written = write(fd, command, strlen(command));
	close(fd);
	return written < 0;
}


static void
test_digital(const char* name, const char* firstPin)
{
	printf("%s\n", name);
	BDigitalPort port;
	check(!port.IsOpen() && port.Read(NULL) == B_ERROR,
		"not open: IsOpen() false, Read() fails");
	status_t status = port.Open(name);
	check(status > 0 && port.IsOpen(), "Open() returns a positive number");
	check(port.IsInput() && !port.IsOutput(), "opens as an input");
	check(port.Write(0x55) == B_ERROR, "Write() on an input fails");
	uint8 value = 0;
	check(port.Read(&value) == 1, "Read() of an input returns 1");
	printf("      input levels %#04x\n", value);

	BDigitalPort second;
	check(second.Open(name) == B_ERROR, "a second Open() of the port fails");
	char command[32];
	snprintf(command, sizeof(command), "%s out 1\n", firstPin);
	check(text_refused(command), "the gpio command leaves its pins alone");

	check(port.SetAsOutput() == B_OK && port.IsOutput() && !port.IsInput(),
		"SetAsOutput()");
	int bad = 0;
	for (int pattern = 0; pattern < 256; pattern++) {
		uint8 readBack = 0;
		if (port.Write(pattern) != 1 || port.Read(&readBack) != 1
			|| readBack != pattern) {
			if (bad++ < 4) {
				printf("      wrote %#04x, read %#04x\n", pattern,
					readBack);
			}
		}
	}
	check(bad == 0, "all 256 values written read back from the pins");

	const int kRounds = 20000;
	bigtime_t start = system_time();
	for (int i = 0; i < kRounds; i++) {
		port.Write(i & 1 ? 0xff : 0);
		port.Read(&value);
	}
	bigtime_t took = system_time() - start;
	printf("      %d writes and reads in %lld ms: %.0f a second each\n",
		kRounds, (long long)took / 1000, kRounds * 1000000.0 / took);

	check(port.SetAsInput() == B_OK && port.IsInput(), "SetAsInput()");
	port.Write(0);
	port.Close();
	check(!port.IsOpen() && !port.IsInput() && !port.IsOutput(),
		"Close(): not open, neither input nor output");
	check(second.Open(name) > 0, "open again after Close()");
	check(second.IsInput(), "  as an input");
	second.Close();
}


static void
test_adc()
{
	int fd = open(MESON_SARADC_DEVICE_DIRECTORY "/0", O_RDONLY);
	check(fd >= 0, "/dev/misc/adc/0 opens");
	if (fd < 0)
		return;
	static const char* const kNames[] = { "ground", "1/4", "1/2", "3/4",
		"all" };
	for (uint32 i = 0; i <= 4; i++) {
		meson_saradc_reference reference;
		reference.reference = i;
		bool read = ioctl(fd, MESON_SARADC_READ_REFERENCE, &reference,
			sizeof(reference)) == 0;
		// the converter within 2 % of the ideal; corrected, on the spot
		uint32 expected = i == 4 ? 1023 : i * 256;
		int32 error = (int32)reference.uncalibrated - (int32)expected;
		int32 corrected = (int32)reference.raw - (int32)expected;
		char what[96];
		snprintf(what, sizeof(what), "reference %s: %" B_PRIu32
			" (uncorrected %" B_PRIu32 "), expected %" B_PRIu32, kNames[i],
			read ? reference.raw : 0, read ? reference.uncalibrated : 0,
			expected);
		check(read && error >= -20 && error <= 20 && corrected >= -2
			&& corrected <= 2, what);
	}
	close(fd);

	static const char* const kChannels[] = { "A2D1", "A2D2" };
	for (int c = 0; c < 2; c++) {
		BA2D adc;
		check(adc.Open(kChannels[c]) > 0 && adc.IsOpen(), kChannels[c]);
		ushort value = 0, low = 0xffff, high = 0;
		uint32 sum = 0;
		bool good = true;
		const int kSamples = 200;
		bigtime_t start = system_time();
		for (int i = 0; i < kSamples; i++) {
			if (adc.Read(&value) != 2 || value > 1800)
				good = false;
			low = value < low ? value : low;
			high = value > high ? value : high;
			sum += value;
		}
		bigtime_t took = system_time() - start;
		check(good, "  Read() returns 2 and 0 to 1800 mV");
		printf("      %d samples (nothing connected): mean %u mV, %u to %u; "
			"%.0f a second\n", kSamples, (unsigned)(sum / kSamples), low,
			high, kSamples * 1000000.0 / took);
		adc.Close();
		check(!adc.IsOpen() && adc.Read(&value) == B_ERROR,
			"  closed: Read() fails");
	}
	BA2D adc;
	check(adc.Open("A2D3") == B_ERROR, "Open(\"A2D3\") fails");
}


/*!	The share of samples in which the pin reads high: the pin is claimed
	without changing it, and read at random times (reads at a steady rate
	beat with the PWM period and miss its share by a few per cent).
*/
static double
measure_duty(uint8 pin, int samples)
{
	int fd = open(MESON_GPIO_DEVICE, O_RDWR);
	meson_gpio_pins pins;
	pins.count = 1;
	pins.pins[0] = pin;
	if (fd < 0 || ioctl(fd, MESON_GPIO_CLAIM, &pins, sizeof(pins)) != 0) {
		if (fd >= 0)
			close(fd);
		return -1;
	}
	int highs = 0;
	for (int i = 0; i < samples; i++) {
		for (volatile int wait = rand() % 64; wait > 0; wait--)
			;
		uint32 levels = 0;
		ioctl(fd, MESON_GPIO_READ, &levels, sizeof(levels));
		highs += levels & 1;
	}
	close(fd);
	return (double)highs / samples;
}


static void
test_pwm()
{
	static const struct { const char* name; uint8 pin; } kChannels[] = {
		{ "D2A1", 11 }, { "D2A2", 19 } };
	for (int c = 0; c < 2; c++) {
		printf("%s (pin %u)\n", kChannels[c].name, kChannels[c].pin);
		BD2A d2a;
		check(d2a.Open(kChannels[c].name) > 0, "Open()");
		BD2A second;
		check(second.Open(kChannels[c].name) == B_ERROR,
			"a second Open() fails");
		uint8 value = 0;
		check(d2a.Write(77) == 1 && d2a.Read(&value) == 1 && value == 77,
			"Read() gives the value written");
		d2a.Close();
		check(second.Open(kChannels[c].name) > 0
			&& second.Read(&value) == 1 && value == 77,
			"  and after Close(), to the next opener");
		second.Close();

		static const uint8 kValues[] = { 0, 1, 2, 64, 128, 192, 253, 254,
			255 };
		const int kSamples = 200000;
		for (size_t i = 0; i < B_COUNT_OF(kValues); i++) {
			BD2A channel;
			if (channel.Open(kChannels[c].name) <= 0
				|| channel.Write(kValues[i]) != 1) {
				check(false, "Write()");
				continue;
			}
			channel.Close();
				// the output stays; the pin is free for measuring
			double duty = measure_duty(kChannels[c].pin, kSamples);
			double expected = kValues[i] / 255.0;
			char what[96];
			snprintf(what, sizeof(what), "value %3u: high %6.2f %%, "
				"expected %6.2f %%", kValues[i], duty * 100, expected * 100);
			// 200000 samples: about 0.1 % of statistical error
			check(duty >= 0 && duty > expected - 0.006
				&& duty < expected + 0.006, what);
		}
		// the pin as before the test (an input)
		char command[32];
		snprintf(command, sizeof(command), "%u release\n",
			kChannels[c].pin);
		int fd = open(MESON_GPIO_DEVICE, O_WRONLY);
		if (fd >= 0) {
			write(fd, command, strlen(command));
			close(fd);
		}
	}
	BD2A d2a;
	check(d2a.Open("D2A3") == B_ERROR, "Open(\"D2A3\") fails (pin 33 has "
		"no PWM: its controller is the core regulator's)");
	check(text_refused("33 pwm 128\n"), "the gpio command gives pin 33 no "
		"PWM either");
	check(text_refused("3 out 1\n") && text_refused("5 in\n"),
		"pins 3 and 5 (the RTC shield) aren't offered");
}


static void
test_pin()
{
	BDigitalPin pin;
	int32 count = pin.CountDevices();
	char name[B_OS_NAME_LENGTH] = "";
	check(count == 23 && pin.GetDeviceName(0, name) == B_OK
		&& strcmp(name, "Pin7") == 0, "CountDevices() 23, the first Pin7");
	check(pin.GetDeviceName(count, name) == B_BAD_INDEX,
		"GetDeviceName() past the last fails");
	check(pin.Open("Pin3") != B_OK, "Pin3 (the RTC shield's I2C) can't be "
		"opened");
	check(pin.Open("Pin7") == B_OK && pin.IsInput(), "Open(\"Pin7\"): an "
		"input");
	BDigitalPin second;
	check(second.Open("GPIOX_21") == B_BUSY, "the same pin by its SoC name: "
		"B_BUSY");
	BDigitalPort port;
	check(port.Open("DigitalA") == B_ERROR, "DigitalA, which has pin 7, "
		"fails");
	check(text_refused("7 out 1\n"), "the gpio command leaves it alone");

	bool level = false;
	// the pulls on pin 12: pin 7 is pulled up on this board's RTC shield
	BDigitalPin pin12;
	check(pin12.Open("Pin12") == B_OK && pin12.SetPull(B_PIN_PULL_UP) == B_OK
		&& pin12.Read(&level) == B_OK && level,
		"pin 12, pull-up: an open input reads high");
	check(pin12.SetPull(B_PIN_PULL_DOWN) == B_OK && pin12.Read(&level) == B_OK
		&& !level, "  pull-down: it reads low");
	pin12.SetPull(B_PIN_PULL_UP);
	pin12.Close();
	check(pin.Write(true) == B_NOT_ALLOWED, "Write() on an input: "
		"B_NOT_ALLOWED");
	check(pin.SetAsOutput(true) == B_OK && pin.IsOutput()
		&& pin.Read(&level) == B_OK && level, "SetAsOutput(true) reads high");
	check(pin.Write(false) == B_OK && pin.Read(&level) == B_OK && !level,
		"Write(false) reads low (against the shield's pull-up)");
	check(pin.SetAsInput() == B_OK && pin.SetPull(B_PIN_PULL_UP) == B_OK,
		"back to an input with pull-up");
	pin.Close();
	check(!pin.IsOpen() && second.Open("GPIOX_21") == B_OK,
		"after Close() it opens by its SoC name");
	second.Close();
	check(pin.Open("Pin41") == B_ENTRY_NOT_FOUND, "Pin41: no such pin");
}


/*!	High share and rising edges a second, sampling the pin for \a seconds
	at random times. The pin may be someone else's: its level comes from
	the pin information, which needs no claim.
*/
static void
measure_wave(uint8 pin, double seconds, double* _duty, double* _frequency)
{
	*_duty = -1;
	*_frequency = -1;
	int fd = open(MESON_GPIO_DEVICE, O_RDONLY);
	if (fd < 0)
		return;
	meson_gpio_pin_info info;
	for (info.index = 0; ioctl(fd, MESON_GPIO_GET_PIN_INFO, &info,
			sizeof(info)) == 0 && info.pin != pin; info.index++)
		;
	if (info.pin != pin) {
		close(fd);
		return;
	}
	int64 samples = 0;
	int64 highs = 0;
	int64 rises = 0;
	bool last = false;
	bigtime_t start = system_time();
	bigtime_t end = start + (bigtime_t)(seconds * 1000000);
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
	close(fd);
	*_duty = (double)highs / samples;
	*_frequency = rises / seconds;
}


static void
test_bpwm()
{
	BPWM pwm;
	char name[B_OS_NAME_LENGTH] = "";
	check(pwm.CountDevices() == 2 && pwm.GetDeviceName(1, name) == B_OK
		&& strcmp(name, "PWM2") == 0, "CountDevices() 2: PWM1, PWM2");
	check(pwm.Open("PWM1") == B_OK && !pwm.IsRunning(), "Open(\"PWM1\"): "
		"stopped");
	BD2A d2a;
	check(d2a.Open("D2A1") == B_ERROR, "D2A1, the same pin, fails");
	pwm.Close();

	struct { const char* what; float hertz; float duty; uint32 width; }
	const kCases[] = {
		{ "servo: 50 Hz, 1.5 ms", 50, -1, 1500000 },
		{ "50 Hz, 25 %", 50, 0.25f, 0 },
		{ "1 kHz, the 25 % kept", 1000, -1, 0 },
		{ "1 kHz, 80 %", 1000, 0.8f, 0 },
	};
	check(pwm.Open("PWM1") == B_OK, "open again");
	double duty, frequency;
	measure_wave(11, 0.5, &duty, &frequency);
	check(duty == 0, "stopped: the pin low");
	for (size_t i = 0; i < B_COUNT_OF(kCases); i++) {
		status_t status = pwm.SetFrequency(kCases[i].hertz);
		if (status == B_OK && kCases[i].duty >= 0)
			status = pwm.SetDutyCycle(kCases[i].duty);
		else if (status == B_OK && kCases[i].width > 0)
			status = pwm.SetPulseWidth(kCases[i].width);
		if (status == B_OK)
			status = pwm.Start();
		double expected = (double)pwm.PulseWidth() / pwm.Period();
		measure_wave(11, 2.0, &duty, &frequency);
		char what[160];
		snprintf(what, sizeof(what), "%s: period %" B_PRIu32 " ns, pulse %"
			B_PRIu32 " ns; measured %.2f %% high (%.2f %%), %.1f Hz",
			kCases[i].what, pwm.Period(), pwm.PulseWidth(), duty * 100,
			expected * 100, frequency);
		check(status == B_OK && duty > expected - 0.01
			&& duty < expected + 0.01 && frequency > kCases[i].hertz * 0.97
			&& frequency < kCases[i].hertz * 1.03, what);
	}
	check(pwm.Stop() == B_OK && !pwm.IsRunning(), "Stop()");
	measure_wave(11, 0.5, &duty, &frequency);
	check(duty == 0, "  the pin low");
	pwm.Start();
	pwm.Close();
	measure_wave(11, 0.5, &duty, &frequency);
	check(duty == 0 && d2a.Open("D2A1") > 0, "Close() while running: the pin "
		"low, and D2A1 opens");
	d2a.Close();
	int fd = open(MESON_GPIO_DEVICE, O_WRONLY);
	if (fd >= 0) {
		write(fd, "11 release\n", 11);
		close(fd);
	}
}


static void
test_geekport()
{
	check(BGeekPort::CountPins() == 40, "CountPins() 40");
	geekport_pin_info info;
	int32 power = 0, ground = 0, gpio = 0, analog = 0;
	for (int32 pin = 1; pin <= 40; pin++) {
		if (BGeekPort::GetPinInfo(pin, &info) != B_OK) {
			check(false, "GetPinInfo()");
			continue;
		}
		power += (info.capabilities & B_GEEKPORT_POWER) != 0;
		ground += (info.capabilities & B_GEEKPORT_GROUND) != 0;
		gpio += (info.capabilities & B_GEEKPORT_GPIO) != 0;
		analog += (info.capabilities & B_GEEKPORT_ANALOG) != 0;
		printf("      %2" B_PRId32 "  %-9s  %-7s  %-7s  %s\n", pin, info.name,
			info.function, info.user, (info.capabilities & B_GEEKPORT_PWM)
				? "PWM" : (info.capabilities & B_GEEKPORT_UART) ? "UART"
				: (info.capabilities & B_GEEKPORT_I2C) ? "I2C" : "");
	}
	check(power == 5 && ground == 8 && gpio == 23 && analog == 2,
		"5 power, 8 ground, 23 GPIO and 2 analog pins");
	check(BGeekPort::GetPinInfo(3, &info) == B_OK
		&& strcmp(info.user, "I2C A") == 0, "pin 3: I2C A has it");
	BDigitalPin pin;
	pin.Open("Pin7");
	check(BGeekPort::GetPinInfo(7, &info) == B_OK
		&& strcmp(info.user, "program") == 0 && info.team == getpid(),
		"pin 7, opened here: \"program\", this team");
	pin.Close();
	check(BGeekPort::GetPinInfo(7, &info) == B_OK && info.user[0] == '\0',
		"after Close(): free");
	check(BGeekPort::GetPinInfo(41, &info) == B_BAD_INDEX, "pin 41: "
		"B_BAD_INDEX");
}


/*!	Counts B_DIGITAL_PIN_CHANGED messages from pin 12. */
class EdgeCounter : public BLooper {
public:
	EdgeCounter()
		:
		BLooper("edge counter"),
		rises(0),
		falls(0),
		bad(0),
		missed(0)
	{
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != B_DIGITAL_PIN_CHANGED) {
			BLooper::MessageReceived(message);
			return;
		}
		int32 edge = message->GetInt32("edge", 0);
		bool level = message->GetBool("level", false);
		if (message->GetInt32("pin", 0) != 12 || !message->HasInt64("when"))
			bad++;
		else if (edge == B_PIN_RISING_EDGE && level)
			rises++;
		else if (edge == B_PIN_FALLING_EDGE && !level)
			falls++;
		else
			bad++;
		missed += message->GetInt32("missed", 0);
	}

	vint32	rises, falls, bad, missed;
};


/*!	Reads the recorded edges without waiting: how many, whether they
	alternate starting with a rising one (levels matching, times rising).
*/
static int32
read_edges(BDigitalPin& pin, pin_edge edge, bool* _alternating)
{
	int32 count = 0;
	bool alternating = true;
	bigtime_t last = 0;
	bool level;
	bigtime_t when;
	while (pin.WaitForEdge(edge, 0, &level, &when) == B_OK) {
		if (level != (count % 2 == 0) || when < last)
			alternating = false;
		last = when;
		count++;
	}
	if (_alternating != NULL)
		*_alternating = alternating;
	return count;
}


static void
test_edge()
{
	BDigitalPin pin;
	check(pin.Open("Pin12") == B_OK && pin.SetAsOutput(false) == B_OK,
		"Pin12 as an output, low");
	bigtime_t start = system_time();
	status_t status = pin.WaitForEdge(B_PIN_RISING_EDGE, 50000);
	bigtime_t waited = system_time() - start;
	check(status == B_TIMED_OUT && waited >= 50000 && waited < 70000,
		"no edge: B_TIMED_OUT after 50 ms");

	bool level = false;
	bigtime_t when = 0;
	bigtime_t before = system_time();
	pin.Write(true);
	status = pin.WaitForEdge(B_PIN_RISING_EDGE, 100000, &level, &when);
	char text[128];
	snprintf(text, sizeof(text), "Write(true): a rising edge, high, %" B_PRId64
		" us after the write began", when - before);
	check(status == B_OK && level && when >= before && when - before < 1000,
		text);
	pin.Write(false);
	check(pin.WaitForEdge(B_PIN_RISING_EDGE, 20000) == B_TIMED_OUT,
		"a falling edge isn't a rising one");

	// starting on a pin already at the level the edge leads to
	pin.Write(true);
	pin.WaitForEdge(B_PIN_RISING_EDGE, 20000);
	check(pin.WaitForEdge(B_PIN_FALLING_EDGE, 20000) == B_TIMED_OUT
		&& pin.WaitForEdge(B_PIN_RISING_EDGE, 20000) == B_TIMED_OUT,
		"starting on a high pin makes no edge");
	pin.Write(false);
	check(pin.WaitForEdge(B_PIN_FALLING_EDGE, 20000) == B_TIMED_OUT,
		"starting on a low pin makes no falling edge");

	// both edges, 200 us apart
	pin.WaitForEdge(B_PIN_ANY_EDGE, 0);
	for (int i = 0; i < 20; i++) {
		pin.Write(i % 2 == 0);
		snooze(200);
	}
	bool alternating;
	int32 count = read_edges(pin, B_PIN_ANY_EDGE, &alternating);
	snprintf(text, sizeof(text), "any edge, 20 writes 200 us apart: %" B_PRId32
		" edges, alternating", count);
	check(count == 20 && alternating, text);

	// as fast as the writes go
	before = system_time();
	for (int i = 0; i < 20; i++)
		pin.Write(i % 2 == 0);
	bigtime_t took = system_time() - before;
	snooze(1000);
	count = read_edges(pin, B_PIN_ANY_EDGE, &alternating);
	snprintf(text, sizeof(text), "20 writes in %" B_PRId64 " us: %" B_PRId32
		" edges, %salternating", took, count, alternating ? "" : "not ");
	check(count == 20 && alternating, text);

	// a full queue: 40 rising edges, 32 wait
	pin.WaitForEdge(B_PIN_RISING_EDGE, 0);
	for (int i = 0; i < 40; i++) {
		pin.Write(true);
		snooze(100);
		pin.Write(false);
		snooze(100);
	}
	status = pin.WaitForEdge(B_PIN_RISING_EDGE, 0, &level, &when);
	count = read_edges(pin, B_PIN_RISING_EDGE, NULL) + (status == B_OK);
	snprintf(text, sizeof(text), "40 rising edges unread: %" B_PRId32
		" kept", count);
	check(count == 32, text);
	// (missed is only in StartWatching()'s messages and the driver)

	// StartWatching()
	EdgeCounter* counter = new EdgeCounter;
	counter->Run();
	check(pin.StartWatching(BMessenger(counter), B_PIN_ANY_EDGE) == B_OK,
		"StartWatching(any edge)");
	check(pin.WaitForEdge(B_PIN_RISING_EDGE, 0) == B_BUSY,
		"WaitForEdge() while watching: B_BUSY");
	for (int i = 0; i < 10; i++) {
		pin.Write(i % 2 == 0);
		snooze(1000);
	}
	snooze(50000);
	snprintf(text, sizeof(text), "10 writes: %" B_PRId32 " rising and %"
		B_PRId32 " falling messages, %" B_PRId32 " bad", counter->rises,
		counter->falls, counter->bad);
	check(counter->rises == 5 && counter->falls == 5 && counter->bad == 0
		&& counter->missed == 0, text);
	check(pin.StopWatching() == B_OK, "StopWatching()");
	pin.Write(true);
	pin.Write(false);
	snooze(20000);
	check(counter->rises == 5 && counter->falls == 5,
		"no messages after StopWatching()");
	// a burst the watcher can't keep up with: messages for all of them
	check(pin.StartWatching(BMessenger(counter), B_PIN_RISING_EDGE) == B_OK,
		"StartWatching(rising edges) again");
	for (int i = 0; i < 100; i++) {
		pin.Write(true);
		snooze(50);
		pin.Write(false);
		snooze(50);
	}
	snooze(100000);
	snprintf(text, sizeof(text), "100 rising edges 100 us apart: %" B_PRId32
		" messages, %" B_PRId32 " missed", counter->rises - 5,
		counter->missed);
	check(counter->rises - 5 + counter->missed == 100 && counter->bad == 0,
		text);
	pin.StopWatching();
	counter->Lock();
	counter->Quit();

	// the 8 channels: four pins' both edges take them all
	static const char* kOthers[] = { "Pin13", "Pin15", "Pin16", "Pin18" };
	BDigitalPin others[4];
	bool armed = true;
	for (int i = 0; i < 4; i++) {
		armed &= others[i].Open(kOthers[i]) == B_OK
			&& others[i].WaitForEdge(B_PIN_ANY_EDGE, 0) == B_TIMED_OUT;
	}
	check(armed, "both edges of pins 13, 15, 16 and 18");
	check(pin.WaitForEdge(B_PIN_RISING_EDGE, 0) == B_BUSY,
		"then pin 12's rising edges: B_BUSY");
	meson_gpio_pin_info info;
	int fd = open(MESON_GPIO_DEVICE, O_RDONLY);
	int32 recorded = 0;
	for (info.index = 0; ioctl(fd, MESON_GPIO_GET_PIN_INFO, &info,
			sizeof(info)) == 0; info.index++) {
		recorded += (info.flags & MESON_GPIO_PIN_EDGES) != 0;
	}
	check(recorded == 4, "the driver says 4 pins have their edges recorded");
	others[0].Close();
	check(pin.WaitForEdge(B_PIN_ANY_EDGE, 0) == B_TIMED_OUT,
		"after one Close(): pin 12's both edges");
	for (int i = 1; i < 4; i++)
		others[i].Close();
	pin.Close();
	recorded = 0;
	for (info.index = 0; ioctl(fd, MESON_GPIO_GET_PIN_INFO, &info,
			sizeof(info)) == 0; info.index++) {
		recorded += (info.flags & MESON_GPIO_PIN_EDGES) != 0;
	}
	close(fd);
	check(recorded == 0, "after Close(): none recorded");
}


/*!	Pin 8's level changes, sampled as fast as GET_PIN_INFO goes (a few
	microseconds apart), from Start() to Stop().
*/
class PinSampler {
public:
	PinSampler(uint32 pin)
		:
		fFd(open(MESON_GPIO_DEVICE, O_RDONLY)),
		fIndex(0),
		fThread(-1),
		fCount(0),
		fSamples(0)
	{
		meson_gpio_pin_info info;
		for (info.index = 0; fFd >= 0 && ioctl(fFd, MESON_GPIO_GET_PIN_INFO,
				&info, sizeof(info)) == 0; info.index++) {
			if (info.pin == pin) {
				fIndex = info.index;
				break;
			}
		}
	}

	~PinSampler()
	{
		Stop();
		close(fFd);
	}

	void Start()
	{
		fCount = 0;
		fSamples = 0;
		fStop = false;
		fRunning = false;
		fThread = spawn_thread(&_Sample, "pin sampler", B_REAL_TIME_PRIORITY,
			this);
		resume_thread(fThread);
		while (!fRunning)
			snooze(100);
	}

	void Stop()
	{
		if (fThread < 0)
			return;
		fStop = true;
		status_t result;
		wait_for_thread(fThread, &result);
		fThread = -1;
	}

	bool LevelAt(bigtime_t time) const
	{
		bool level = fInitial;
		for (int32 i = 0; i < fCount && fTimes[i] <= time; i++)
			level = fLevels[i];
		return level;
	}

	int32 Count() const { return fCount; }
	bigtime_t TimeAt(int32 i) const { return fTimes[i]; }
	bool LevelOf(int32 i) const { return fLevels[i]; }
	bigtime_t Period() const
		{ return fSamples > 0 ? (fEnd - fBegin) / fSamples : 0; }

	/*!	The frames in the samples: \a bits data bits, \a parity 0 (none),
		1 (odd) or 2 (even), \a stopBits. Returns how many; *_bad counts
		start, parity and stop bits that were wrong.
	*/
	int32 Decode(double bitTime, int bits, int parity, int stopBits,
		uint8* bytes, int32 maxBytes, int32* _bad) const
	{
		int32 count = 0, bad = 0;
		bigtime_t next = 0;
		for (int32 i = 0; i < fCount && count < maxBytes; i++) {
			if (fLevels[i] || fTimes[i] < next)
				continue;
			double start = fTimes[i] - Period() / 2.0;
			if (LevelAt((bigtime_t)(start + bitTime / 2)))
				bad++;
			uint32 value = 0;
			int ones = 0;
			for (int b = 0; b < bits; b++) {
				if (LevelAt((bigtime_t)(start + (1.5 + b) * bitTime))) {
					value |= 1u << b;
					ones++;
				}
			}
			int frameBits = 1 + bits;
			if (parity != 0) {
				bool bit = LevelAt((bigtime_t)(start + (frameBits + 0.5)
					* bitTime));
				if (((ones + bit) % 2 == 1) != (parity == 1))
					bad++;
				frameBits++;
			}
			for (int b = 0; b < stopBits; b++) {
				if (!LevelAt((bigtime_t)(start + (frameBits + 0.5)
						* bitTime))) {
					bad++;
				}
				frameBits++;
			}
			bytes[count++] = value;
			next = (bigtime_t)(start + (frameBits - 0.5) * bitTime);
		}
		*_bad = bad;
		return count;
	}

private:
	static status_t _Sample(void* data)
	{
		PinSampler* sampler = (PinSampler*)data;
		meson_gpio_pin_info info;
		info.index = sampler->fIndex;
		ioctl(sampler->fFd, MESON_GPIO_GET_PIN_INFO, &info, sizeof(info));
		bool last = (info.flags & MESON_GPIO_PIN_LEVEL) != 0;
		sampler->fInitial = last;
		sampler->fBegin = system_time();
		sampler->fRunning = true;
		while (!sampler->fStop) {
			ioctl(sampler->fFd, MESON_GPIO_GET_PIN_INFO, &info, sizeof(info));
			bigtime_t now = system_time();
			sampler->fSamples++;
			bool level = (info.flags & MESON_GPIO_PIN_LEVEL) != 0;
			if (level != last && sampler->fCount < kMaxChanges) {
				sampler->fTimes[sampler->fCount] = now;
				sampler->fLevels[sampler->fCount] = level;
				sampler->fCount++;
				last = level;
			}
		}
		sampler->fEnd = system_time();
		return B_OK;
	}

	static const int32 kMaxChanges = 200000;

	int				fFd;
	uint32			fIndex;
	thread_id		fThread;
	volatile bool	fStop;
	volatile bool	fRunning;
	bool			fInitial;
	int32			fCount;
	int64			fSamples;
	bigtime_t		fBegin, fEnd;
	bigtime_t		fTimes[kMaxChanges];
	bool			fLevels[kMaxChanges];
};


/*!	The UART's real speed for a nominal one: 8 MHz / round(8 MHz / speed). */
static double
uart_speed(uint32 speed)
{
	return 8000000.0 / ((8000000 + speed / 2) / speed);
}


/*!	Sends \a text with the port's format, and decodes it from pin 8. */
static void
check_frames(BSerialPort& port, PinSampler& sampler, int bits, int parity,
	int stopBits, const char* what)
{
	port.SetDataBits(bits == 8 ? B_DATA_BITS_8 : B_DATA_BITS_7);
	port.SetParityMode(parity == 0 ? B_NO_PARITY
		: parity == 1 ? B_ODD_PARITY : B_EVEN_PARITY);
	port.SetStopBits(stopBits == 2 ? B_STOP_BITS_2 : B_STOP_BITS_1);
	static const uint8 kBytes[] = { 'G', 'e', 'e', 'k', 'P', 'o', 'r', 't',
		0x00, 0x7f, 0x55, 0x2a, 0x01, 0x40 };
	uint8 sent[sizeof(kBytes) + 2];
	memcpy(sent, kBytes, sizeof(kBytes));
	sent[sizeof(kBytes)] = bits == 8 ? 0xff : 0x3c;
	sent[sizeof(kBytes) + 1] = bits == 8 ? 0xa5 : 0x5a;
	snooze(20000);
	sampler.Start();
	snooze(2000);
	ssize_t written = port.Write(sent, sizeof(sent));
	snooze(sizeof(sent) * 12 * 1000000LL / 9600 + 10000);
	sampler.Stop();
	uint8 decoded[64];
	int32 bad;
	int32 count = sampler.Decode(1000000.0 / uart_speed(9600), bits, parity,
		stopBits, decoded, sizeof(decoded), &bad);
	char text[160];
	snprintf(text, sizeof(text), "%s: %zd bytes written, %" B_PRId32
		" frames decoded from pin 8 (a sample every %" B_PRId64 " us), %"
		B_PRId32 " bad bits", what, written, count, sampler.Period(), bad);
	check(written == (ssize_t)sizeof(sent) && count == (int32)sizeof(sent)
		&& bad == 0 && memcmp(decoded, sent, sizeof(sent)) == 0, text);
	if (count != (int32)sizeof(sent) || memcmp(decoded, sent, count) != 0) {
		printf("      decoded:");
		for (int32 i = 0; i < count; i++)
			printf(" %02x", decoded[i]);
		printf("\n");
	}
}


/*!	Writes \a count zero bytes at \a speed and measures the speed from the
	first start bit to the last stop bit: (count - 1) * 10 + 9 bit times,
	unless the transmitter ran dry somewhere.
*/
static void
check_speed(int fd, PinSampler& sampler, uint32 speed, int32 count)
{
	termios config;
	tcgetattr(fd, &config);
	cfsetospeed(&config, speed);
	cfsetispeed(&config, speed);
	config.c_cflag &= ~(PARENB | CSTOPB);
	config.c_cflag |= CS8;
	tcsetattr(fd, TCSANOW, &config);
	uint8* zeros = (uint8*)calloc(count, 1);
	snooze(10000);
	sampler.Start();
	snooze(2000);
	ssize_t written = write(fd, zeros, count);
	snooze((bigtime_t)(count * 10 * 1000000.0 / speed) + 20000);
	sampler.Stop();
	free(zeros);

	bigtime_t first = -1, last = -1;
	for (int32 i = 0; i < sampler.Count(); i++) {
		if (!sampler.LevelOf(i) && first < 0)
			first = sampler.TimeAt(i);
		if (sampler.LevelOf(i))
			last = sampler.TimeAt(i);
	}
	double measured = first >= 0 && last > first
		? ((count - 1) * 10.0 + 9) * 1000000.0 / (last - first) : 0;
	double expected = uart_speed(speed);
	char text[160];
	snprintf(text, sizeof(text), "%" B_PRIu32 " bits/s: %zd bytes, measured "
		"%.0f, the UART's %.0f (%+.2f %%)", speed, written, measured,
		expected, (measured / expected - 1) * 100);
	check(written == count && measured > expected * 0.995
		&& measured < expected * 1.005, text);
}


static bool
mem_read(int fd, uint32 address, uint32* value)
{
	return pread(fd, value, 4, address) == 4;
}


static bool
mem_write(int fd, uint32 address, uint32 value)
{
	return pwrite(fd, &value, 4, address) == 4;
}


/*!	Makes \a bytes arrive at 1200 bits/s, 8N1, on pin 10 with nothing
	wired to it: its pull-down is a 0 bit, its pull-up a 1 (the pulls work
	whatever the mux selects). The X bank's pull word (1: up) and pull enable
	word.
*/
static bool
drive_rx(const uint8* bytes, int32 count)
{
	const uint32 kPull = 0xc88344f8, kPullEnable = 0xc8834530;
	const uint32 kBit = 1u << 13;
	int fd = open("/dev/misc/mem", O_RDWR);
	uint32 pull, enable;
	if (fd < 0 || !mem_read(fd, kPull, &pull)
		|| !mem_read(fd, kPullEnable, &enable)) {
		close(fd);
		return false;
	}
	mem_write(fd, kPull, pull | kBit);
	mem_write(fd, kPullEnable, enable | kBit);
	snooze(20000);

	thread_id thread = find_thread(NULL);
	int32 priority = set_thread_priority(thread, B_REAL_TIME_PRIORITY);
	const double kBitTime = 1000000.0 / 1200;
	bigtime_t start = system_time() + 1000;
	int32 bit = 0;
	for (int32 i = 0; i < count; i++) {
		// start bit, 8 data bits from the lowest, a stop bit
		uint32 frame = (1u << 9) | ((uint32)bytes[i] << 1);
		for (int b = 0; b < 10; b++, bit++) {
			bigtime_t when = start + (bigtime_t)(bit * kBitTime);
			while (system_time() < when)
				;
			mem_write(fd, kPull, ((frame >> b) & 1) != 0
				? pull | kBit : pull & ~kBit);
		}
	}
	while (system_time() < start + (bigtime_t)((bit + 1) * kBitTime))
		;
	set_thread_priority(thread, priority);
	mem_write(fd, kPull, pull);
	mem_write(fd, kPullEnable, enable);
	close(fd);
	return true;
}


static void
test_serial()
{
	BSerialPort port;
	char name[B_OS_NAME_LENGTH];
	bool listed = false;
	for (int32 i = 0; i < port.CountDevices(); i++) {
		if (port.GetDeviceName(i, name) == B_OK && strcmp(name, "header") == 0)
			listed = true;
	}
	check(listed, "BSerialPort lists \"header\"");
	check(port.Open("header") > 0, "Open(\"header\")");
	BSerialPort second;
	check(second.Open("header") < 0, "a second Open() fails");
	geekport_pin_info info;
	check(BGeekPort::GetPinInfo(8, &info) == B_OK
		&& strcmp(info.user, "UART A") == 0
		&& strcmp(info.function, "UART A") == 0
		&& BGeekPort::GetPinInfo(10, &info) == B_OK
		&& strcmp(info.user, "UART A") == 0,
		"pins 8 and 10: UART A has them, and selects them");
	BDigitalPin pin;
	check(pin.Open("Pin8") == B_BUSY, "BDigitalPin can't open pin 8");
	check(text_refused("10 out 1\n"), "the gpio command leaves pin 10 "
		"alone");

	PinSampler sampler(8);
	port.SetDataRate(B_9600_BPS);
	port.SetFlowControl(0);
	check_frames(port, sampler, 8, 0, 1, "9600 8N1");
	check_frames(port, sampler, 8, 2, 1, "9600 8E1");
	check_frames(port, sampler, 7, 1, 2, "9600 7O2");

	// receiving, from pin 10 driven as a GPIO output
	port.SetDataBits(B_DATA_BITS_8);
	port.SetParityMode(B_NO_PARITY);
	port.SetStopBits(B_STOP_BITS_1);
	port.SetDataRate(B_1200_BPS);
	port.SetBlocking(true);
	port.SetTimeout(200000);
	port.ClearInput();
	static const uint8 kReceive[] = { 'B', 'e', 'B', 'o', 'x', 0x00, 0xff,
		0x55, 0xaa, '\r', '\n' };
	bool driven = drive_rx(kReceive, sizeof(kReceive));
	snooze(20000);
	int32 available = 0;
	port.NumCharsAvailable(&available);
	uint8 received[64];
	ssize_t length = 0;
	while (length < (ssize_t)sizeof(received)) {
		ssize_t bytes = port.Read(received + length,
			sizeof(received) - length);
		if (bytes <= 0)
			break;
		length += bytes;
	}
	char text[160];
	snprintf(text, sizeof(text), "1200 bits/s from pin 10's pulls: %"
		B_PRId32 " waiting, %zd read", available, length);
	check(driven && available == (int32)sizeof(kReceive)
		&& length == (ssize_t)sizeof(kReceive)
		&& memcmp(received, kReceive, length) == 0, text);
	if (length != (ssize_t)sizeof(kReceive)) {
		printf("      read:");
		for (ssize_t i = 0; i < length; i++)
			printf(" %02x", received[i]);
		printf("\n");
	}
	bigtime_t before = system_time();
	length = port.Read(received, sizeof(received));
	bigtime_t waited = system_time() - before;
	snprintf(text, sizeof(text), "nothing more: Read() returns %zd after %"
		B_PRId64 " ms (the timeout: 0.2 s)", length, waited / 1000);
	check(length == 0 && waited >= 150000 && waited < 400000, text);
	port.Close();
	geekport_pin_info info10;
	BGeekPort::GetPinInfo(8, &info);
	BGeekPort::GetPinInfo(10, &info10);
	snprintf(text, sizeof(text), "Close(): pins 8 and 10 free GPIO again "
		"(\"%s\" %s, \"%s\" %s)", info.user, info.function, info10.user,
		info10.function);
	check(info.user[0] == '\0' && strcmp(info.function, "input") == 0
		&& info10.user[0] == '\0' && strcmp(info10.function, "input") == 0,
		text);

	// speeds and a break, through termios
	int fd = open("/dev/ports/header", O_RDWR);
	check(fd >= 0, "open(\"/dev/ports/header\")");
	if (fd < 0)
		return;
	termios config;
	tcgetattr(fd, &config);
	config.c_lflag &= ~(ICANON | ECHO | ISIG);
	config.c_oflag &= ~OPOST;
	tcsetattr(fd, TCSANOW, &config);
	check_speed(fd, sampler, 9600, 200);
	check_speed(fd, sampler, 115200, 2000);
	check_speed(fd, sampler, 500000, 4000);
	check_speed(fd, sampler, 1000000, 4000);
	check_speed(fd, sampler, 2000000, 8000);
	check_speed(fd, sampler, 4000000, 8000);
	meson_gpio_pin_info pinInfo;
	int gpio = open(MESON_GPIO_DEVICE, O_RDONLY);
	for (pinInfo.index = 0; ioctl(gpio, MESON_GPIO_GET_PIN_INFO, &pinInfo,
			sizeof(pinInfo)) == 0 && pinInfo.pin != 8; pinInfo.index++)
		;
	bool idle = (pinInfo.flags & MESON_GPIO_PIN_LEVEL) != 0;
	ioctl(fd, TIOCSBRK, 0);
	ioctl(gpio, MESON_GPIO_GET_PIN_INFO, &pinInfo, sizeof(pinInfo));
	bool breaking = (pinInfo.flags & MESON_GPIO_PIN_LEVEL) != 0;
	ioctl(fd, TIOCCBRK, 0);
	ioctl(gpio, MESON_GPIO_GET_PIN_INFO, &pinInfo, sizeof(pinInfo));
	bool after = (pinInfo.flags & MESON_GPIO_PIN_LEVEL) != 0;
	close(gpio);
	check(idle && !breaking && after, "TIOCSBRK holds pin 8 low, TIOCCBRK "
		"lets it go");
	close(fd);
}


static void
test_i2c()
{
	BI2CDevice device;
	char name[B_OS_NAME_LENGTH];
	check(device.CountDevices() == 1 && device.GetDeviceName(0, name) == B_OK
		&& strcmp(name, "header") == 0, "one bus: \"header\"");
	geekport_pin_info info, info28;
	check(BGeekPort::GetPinInfo(27, &info) == B_OK && info.user[0] == '\0'
		&& strcmp(info.function, "input") == 0, "pin 27: free GPIO before");
	check(device.Open("header", 0x05) == B_BAD_VALUE
		&& device.Open("header", 0x78) == B_BAD_VALUE
		&& device.Open("I2C A", 0x51) == B_ENTRY_NOT_FOUND,
		"addresses 0x08 to 0x77 on \"header\" only (not the RTC's bus)");

	// a program has pin 28 as GPIO
	BDigitalPin pin;
	pin.Open("Pin28");
	check(device.Open("header", 0x50) == B_BUSY, "while a program has pin "
		"28: B_BUSY");
	pin.Close();

	check(device.Open("header", 0x50) == B_OK && device.IsOpen()
		&& device.Address() == 0x50, "Open(\"header\", 0x50)");
	BGeekPort::GetPinInfo(27, &info);
	BGeekPort::GetPinInfo(28, &info28);
	char text[160];
	snprintf(text, sizeof(text), "pins 27 and 28: %s/%s, %s/%s", info.user,
		info.function, info28.user, info28.function);
	check(strcmp(info.user, "I2C B") == 0 && strcmp(info.function, "I2C B")
		== 0 && strcmp(info28.user, "I2C B") == 0, text);
	check(pin.Open("Pin27") == B_BUSY, "BDigitalPin can't open pin 27");
	check(text_refused("28 out 0\n"), "the gpio command leaves pin 28 "
		"alone");

	BI2CDevice second;
	check(second.Open("header", 0x51) == B_OK, "a second device on the bus");

	bigtime_t before = system_time();
	uint8 value = 0;
	uint8 buffer[16];
	status_t readStatus = device.ReadRegister(0, &value);
	ssize_t readLength = device.Read(buffer, sizeof(buffer));
	ssize_t written = device.Write(buffer, 4);
	bool present = device.IsPresent();
	bigtime_t took = system_time() - before;
	snprintf(text, sizeof(text), "nobody at 0x50: ReadRegister() %s, Read() "
		"%zd, Write() %zd, IsPresent() %d (%" B_PRId64 " us for the four)",
		strerror(readStatus), readLength, written, present, took);
	check(readStatus == B_DEV_NOT_READY && readLength == B_DEV_NOT_READY
		&& written == B_DEV_NOT_READY && !present && took < 20000, text);

	// a scan, with pin 28 sampled
	PinSampler sampler(28);
	sampler.Start();
	snooze(1000);
	uint8 found[112];
	int32 count = -1;
	before = system_time();
	status_t status = device.Scan("header", found, sizeof(found), &count);
	took = system_time() - before;
	snooze(1000);
	sampler.Stop();
	int32 falls = 0;
	for (int32 i = 0; i < sampler.Count(); i++)
		falls += !sampler.LevelOf(i);
	snprintf(text, sizeof(text), "Scan(): %s, %" B_PRId32 " found, in %"
		B_PRId64 " ms; SCL fell %" B_PRId32 " times (about 10 an address: "
		"the start, 8 bits, the acknowledge)", strerror(status), count,
		took / 1000, falls);
	check(status == B_OK && count == 0 && falls >= 112 * 9
		&& falls <= 112 * 12, text);

	device.Close();
	BGeekPort::GetPinInfo(27, &info);
	check(strcmp(info.user, "I2C B") == 0, "one device closed: the bus "
		"keeps its pins");
	second.Close();
	BGeekPort::GetPinInfo(27, &info);
	BGeekPort::GetPinInfo(28, &info28);
	snprintf(text, sizeof(text), "the last one closed: pins 27 and 28 free "
		"GPIO again (\"%s\" %s, \"%s\" %s)", info.user, info.function,
		info28.user, info28.function);
	check(info.user[0] == '\0' && strcmp(info.function, "input") == 0
		&& info28.user[0] == '\0' && strcmp(info28.function, "input") == 0,
		text);
	check(pin.Open("Pin27") == B_OK, "and BDigitalPin opens pin 27");
	pin.Close();
	count = -1;
	check(device.Scan("header", found, sizeof(found), &count) == B_OK
		&& count == 0, "Scan() without an open device");
}


int
main(int argc, char** argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	const char* what = argc > 1 ? argv[1] : "digital";
	if (strcmp(what, "digital") == 0) {
		test_digital("DigitalA", "7");
		test_digital("DigitalB", "23");
		BDigitalPort port;
		check(port.Open("DigitalC") == B_ERROR, "Open(\"DigitalC\") fails");
	} else if (strcmp(what, "adc") == 0)
		test_adc();
	else if (strcmp(what, "pwm") == 0)
		test_pwm();
	else if (strcmp(what, "pin") == 0)
		test_pin();
	else if (strcmp(what, "bpwm") == 0)
		test_bpwm();
	else if (strcmp(what, "geekport") == 0)
		test_geekport();
	else if (strcmp(what, "edge") == 0)
		test_edge();
	else if (strcmp(what, "serial") == 0)
		test_serial();
	else if (strcmp(what, "i2c") == 0)
		test_i2c();
	else {
		fprintf(stderr, "usage: geekporttest "
			"[digital|adc|pwm|pin|bpwm|geekport|edge|serial|i2c]\n");
		return 2;
	}
	printf("%s\n", sFailures == 0 ? "all passed" : "FAILURES");
	return sFailures == 0 ? 0 : 1;
}
