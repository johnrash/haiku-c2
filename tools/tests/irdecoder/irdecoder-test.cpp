/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * irdecoder-test: the meson_ir driver's protocol decoders on the build
 * host, fed synthetic frames: exact timings, a remote's clock 5 % off, a
 * receiver's distortion (marks up to 150 us longer, spaces as much
 * shorter), random jitter up to twice a receiver's,
 * frames of different protocols back to back, held buttons, and noise.
 * Optionally decodes a capture: irdecoder-test <file> with one "m <us>" or
 * "s <us>" per line.
 *
 * Build (tools/tests/irdecoder/build.sh):
 *   g++ -O2 -Wall -I. -I<haiku>/headers/private/drivers
 *     -I<haiku>/src/add-ons/kernel/drivers/input/meson_ir
 *     irdecoder-test.cpp <...>/IRDecoder.cpp -o irdecoder-test
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "IRDecoder.h"


using IRRemote::Receiver;


struct Pulse {
	bool	mark;
	uint32	duration;
};

typedef std::vector<Pulse> Train;


static int sFailures = 0;
static int sChecks = 0;


// the distortion applied to every pulse: the remote's clock off by a
// constant factor, the receiver lengthening marks, and random jitter in
// us (a receiver's is about one carrier cycle, 26 us at 38 kHz)
static int sScalePercent = 100;
static int sMarkStretch = 0;
static int sJitter = 0;


static void
Add(Train& train, bool mark, uint32 duration)
{
	// merge with the previous pulse of the same level
	if (!train.empty() && train.back().mark == mark)
		train.back().duration += duration;
	else
		train.push_back(Pulse{mark, duration});
}


static Train
Distort(const Train& train)
{
	Train result;
	for (const Pulse& pulse : train) {
		int duration = pulse.duration * sScalePercent / 100;
		if (pulse.duration < 20000)
			duration += pulse.mark ? sMarkStretch : -sMarkStretch;
		if (sJitter > 0 && pulse.duration < 20000)
			duration += rand() % (2 * sJitter + 1) - sJitter;
		result.push_back(Pulse{pulse.mark, (uint32)(duration < 1 ? 1 : duration)});
	}
	return result;
}


//	#pragma mark - encoders (from the protocols' published timings)


static void
NEC(Train& t, uint32 bits)
{
	Add(t, true, 9000);
	Add(t, false, 4500);
	for (int i = 0; i < 32; i++) {
		Add(t, true, 560);
		Add(t, false, (bits >> i) & 1 ? 1690 : 560);
	}
	Add(t, true, 560);
	Add(t, false, 40000);
}


static void
NECRepeat(Train& t)
{
	Add(t, true, 9000);
	Add(t, false, 2250);
	Add(t, true, 560);
	Add(t, false, 96000);
}


static void
RC5(Train& t, bool toggle, uint32 address, uint32 command)
{
	uint32 bits = (1u << 13) | ((command & 0x40) ? 0 : (1u << 12))
		| (toggle ? (1u << 11) : 0) | ((address & 0x1f) << 6)
		| (command & 0x3f);
	for (int i = 13; i >= 0; i--) {
		bool one = (bits >> i) & 1;
		Add(t, !one, 889);
		Add(t, one, 889);
	}
	Add(t, false, 89000);
}


static void
RC6(Train& t, bool toggle, uint32 address, uint32 command)
{
	Add(t, true, 2666);
	Add(t, false, 889);
	// start bit 1, mode 000
	Add(t, true, 444);
	Add(t, false, 444);
	for (int i = 0; i < 3; i++) {
		Add(t, false, 444);
		Add(t, true, 444);
	}
	Add(t, toggle, 889);
	Add(t, !toggle, 889);
	uint32 bits = (address << 8) | command;
	for (int i = 15; i >= 0; i--) {
		bool one = (bits >> i) & 1;
		Add(t, one, 444);
		Add(t, !one, 444);
	}
	Add(t, false, 83000);
}


static void
Sony(Train& t, int count, uint32 bits)
{
	Add(t, true, 2400);
	for (int i = 0; i < count; i++) {
		Add(t, false, 600);
		Add(t, true, (bits >> i) & 1 ? 1200 : 600);
	}
	Add(t, false, 25000);
}


static void
JVC(Train& t, bool header, uint32 address, uint32 command)
{
	if (header) {
		Add(t, true, 8400);
		Add(t, false, 4200);
	}
	uint32 bits = address | (command << 8);
	for (int i = 0; i < 16; i++) {
		Add(t, true, 526);
		Add(t, false, (bits >> i) & 1 ? 1574 : 526);
	}
	Add(t, true, 526);
	Add(t, false, 22000);
}


//	#pragma mark -


static std::vector<ir_remote_event>
Decode(const Train& train, Receiver* receiver = NULL)
{
	Receiver local;
	if (receiver == NULL)
		receiver = &local;
	std::vector<ir_remote_event> events;
	for (const Pulse& pulse : train) {
		ir_remote_event event;
		if (pulse.mark || pulse.duration < IRRemote::kGap) {
			if (receiver->Feed(pulse.mark, pulse.duration, &event))
				events.push_back(event);
			continue;
		}
		// as the driver does it: after kGap without an edge, Gap(); the
		// edge that ends the space comes later
		if (receiver->Gap(&event))
			events.push_back(event);
		if (receiver->Feed(false, pulse.duration, &event))
			events.push_back(event);
	}
	return events;
}


static const char*
Name(uint32 protocol)
{
	static const char* kNames[] = { "?", "NEC", "NEC-ext", "NEC32", "RC5",
		"RC5X", "RC6", "Sony12", "Sony15", "Sony20", "JVC" };
	return protocol < B_COUNT_OF(kNames) ? kNames[protocol] : "?";
}


static void
Expect(const char* what, const std::vector<ir_remote_event>& events,
	const std::vector<ir_remote_event>& expected)
{
	sChecks++;
	bool ok = events.size() == expected.size();
	for (size_t i = 0; ok && i < events.size(); i++) {
		ok = events[i].protocol == expected[i].protocol
			&& events[i].address == expected[i].address
			&& events[i].command == expected[i].command
			&& events[i].flags == expected[i].flags;
	}
	if (ok)
		return;
	sFailures++;
	printf("FAIL %s (clock %d %%, stretch %d us, jitter %d us):\n  got", what,
		sScalePercent, sMarkStretch, sJitter);
	for (const ir_remote_event& e : events) {
		printf(" %s %#x/%#x%s%s", Name(e.protocol), e.address, e.command,
			e.flags & IR_REMOTE_REPEAT ? " rep" : "",
			e.flags & IR_REMOTE_TOGGLE ? " tog" : "");
	}
	printf("\n  expected");
	for (const ir_remote_event& e : expected) {
		printf(" %s %#x/%#x%s%s", Name(e.protocol), e.address, e.command,
			e.flags & IR_REMOTE_REPEAT ? " rep" : "",
			e.flags & IR_REMOTE_TOGGLE ? " tog" : "");
	}
	printf("\n");
}


static ir_remote_event
E(uint32 protocol, uint32 address, uint32 command, uint32 flags = 0)
{
	ir_remote_event event = {};
	event.protocol = protocol;
	event.address = address;
	event.command = command;
	event.flags = flags;
	return event;
}


static void
RunAll()
{
	const uint32 R = IR_REMOTE_REPEAT;
	const uint32 T = IR_REMOTE_TOGGLE;

	{
		Train t;
		NEC(t, 0x04 | (0xfb << 8) | (0x12 << 16) | (0xedu << 24));
		NECRepeat(t);
		NECRepeat(t);
		Expect("NEC held", Decode(Distort(t)), { E(IR_REMOTE_NEC, 0x04, 0x12),
			E(IR_REMOTE_NEC, 0x04, 0x12, R), E(IR_REMOTE_NEC, 0x04, 0x12, R) });
	}
	{
		Train t;
		NEC(t, 0x4d | (0xb2 << 8) | (0x88 << 16) | (0x77u << 24));
		Expect("NEC Hardkernel-like", Decode(Distort(t)),
			{ E(IR_REMOTE_NEC, 0x4d, 0x88) });
	}
	{
		Train t;
		NEC(t, 0x1234 | (0x05 << 16) | (0xfau << 24));
		Expect("NEC extended", Decode(Distort(t)),
			{ E(IR_REMOTE_NEC_EXTENDED, 0x1234, 0x05) });
	}
	{
		Train t;
		NEC(t, 0x12345678);
		Expect("NEC32", Decode(Distort(t)),
			{ E(IR_REMOTE_NEC32, 0x5678, 0x1234) });
	}
	{
		Train t;
		NECRepeat(t);
		Expect("NEC repeat alone", Decode(Distort(t)), {});
	}
	{
		Train t;
		RC5(t, false, 5, 0x35);
		RC5(t, false, 5, 0x35);
		RC5(t, true, 5, 0x35);
		RC5(t, true, 0, 0x01);
		Expect("RC5", Decode(Distort(t)), { E(IR_REMOTE_RC5, 5, 0x35),
			E(IR_REMOTE_RC5, 5, 0x35, R), E(IR_REMOTE_RC5, 5, 0x35, T),
			E(IR_REMOTE_RC5, 0, 0x01, T) });
	}
	{
		Train t;
		RC5(t, false, 0x1f, 0x45);
		RC5(t, true, 0x10, 0x3e);
		Expect("RC5X", Decode(Distort(t)), { E(IR_REMOTE_RC5X, 0x1f, 0x45),
			E(IR_REMOTE_RC5, 0x10, 0x3e, T) });
	}
	{
		Train t;
		RC6(t, true, 0x04, 0x0c);
		RC6(t, true, 0x04, 0x0c);
		RC6(t, false, 0x04, 0x0c);
		RC6(t, false, 0xff, 0x00);
		RC6(t, false, 0x00, 0xff);
		Expect("RC6", Decode(Distort(t)), { E(IR_REMOTE_RC6, 0x04, 0x0c, T),
			E(IR_REMOTE_RC6, 0x04, 0x0c, T | R), E(IR_REMOTE_RC6, 0x04, 0x0c),
			E(IR_REMOTE_RC6, 0xff, 0x00), E(IR_REMOTE_RC6, 0x00, 0xff) });
	}
	{
		Train t;
		for (int i = 0; i < 3; i++)
			Sony(t, 12, 0x15 | (1 << 7));
		Sony(t, 15, 0x33 | (0x9a << 7));
		Sony(t, 20, 0x7f | (0x1abc << 7));
		Expect("Sony", Decode(Distort(t)), { E(IR_REMOTE_SONY12, 1, 0x15),
			E(IR_REMOTE_SONY12, 1, 0x15, R), E(IR_REMOTE_SONY12, 1, 0x15, R),
			E(IR_REMOTE_SONY15, 0x9a, 0x33),
			E(IR_REMOTE_SONY20, 0x1abc, 0x7f) });
	}
	{
		Train t;
		JVC(t, true, 0xc1, 0x2f);
		JVC(t, false, 0xc1, 0x2f);
		JVC(t, false, 0xc1, 0x2f);
		JVC(t, true, 0xc1, 0x30);
		Expect("JVC held", Decode(Distort(t)), { E(IR_REMOTE_JVC, 0xc1, 0x2f),
			E(IR_REMOTE_JVC, 0xc1, 0x2f, R), E(IR_REMOTE_JVC, 0xc1, 0x2f, R),
			E(IR_REMOTE_JVC, 0xc1, 0x30) });
	}
	{
		// a headerless JVC frame long after the last one is noise
		Train t;
		JVC(t, true, 0x01, 0x02);
		Add(t, false, 500000);
		JVC(t, false, 0x01, 0x02);
		Receiver receiver;
		Expect("JVC late repeat", Decode(Distort(t), &receiver),
			{ E(IR_REMOTE_JVC, 0x01, 0x02) });
	}
	{
		// back to back, every protocol
		Train t;
		JVC(t, true, 0x47, 0x11);
		NEC(t, 0x04 | (0xfb << 8) | (0x12 << 16) | (0xedu << 24));
		RC6(t, false, 0x04, 0x0c);
		Sony(t, 12, 0x15 | (1 << 7));
		RC5(t, false, 5, 0x35);
		JVC(t, true, 0x47, 0x11);
		Expect("mixed", Decode(Distort(t)), { E(IR_REMOTE_JVC, 0x47, 0x11),
			E(IR_REMOTE_NEC, 0x04, 0x12), E(IR_REMOTE_RC6, 0x04, 0x0c),
			E(IR_REMOTE_SONY12, 1, 0x15), E(IR_REMOTE_RC5, 5, 0x35),
			E(IR_REMOTE_JVC, 0x47, 0x11) });
	}
	{
		// every address/command of JVC and NEC, exact and distorted
		int bad = 0;
		for (uint32 a = 0; a < 256; a += 17) {
			for (uint32 c = 0; c < 256; c++) {
				Train t;
				JVC(t, true, a, c);
				NEC(t, a | ((a ^ 0xff) << 8) | (c << 16) | ((c ^ 0xffu) << 24));
				std::vector<ir_remote_event> events = Decode(Distort(t));
				if (events.size() != 2 || events[0].protocol != IR_REMOTE_JVC
					|| events[0].address != a || events[0].command != c
					|| events[1].protocol != IR_REMOTE_NEC
					|| events[1].address != a || events[1].command != c) {
					if (bad++ < 3) {
						printf("  sweep %#x/%#x:", a, c);
						for (const ir_remote_event& e : events)
							printf(" %s %#x/%#x", Name(e.protocol), e.address,
								e.command);
						printf("\n");
					}
				}
			}
		}
		sChecks++;
		if (bad > 0) {
			sFailures++;
			printf("FAIL JVC/NEC sweep (clock %d %%, stretch %d, jitter %d us): "
				"%d bad\n", sScalePercent, sMarkStretch, sJitter, bad);
		}
	}
	{
		// noise: random pulses, then a real frame still decodes
		Receiver receiver;
		Train t;
		for (int burst = 0; burst < 200; burst++) {
			int count = 2 + rand() % 30;
			for (int i = 0; i < count; i++) {
				Add(t, true, 100 + rand() % 3000);
				Add(t, false, 100 + rand() % 3000);
			}
			Add(t, false, 30000);
		}
		std::vector<ir_remote_event> events = Decode(t, &receiver);
		sChecks++;
		if (events.size() > 2) {
			// a random train can look like a frame, rarely
			sFailures++;
			printf("FAIL noise: %zu frames from noise\n", events.size());
		}
		Train frame;
		RC6(frame, false, 0x04, 0x0c);
		Expect("after noise", Decode(Distort(frame), &receiver),
			{ E(IR_REMOTE_RC6, 0x04, 0x0c) });
		sChecks++;
		if (receiver.Unknown() < 150) {
			sFailures++;
			printf("FAIL noise: only %u unknown trains counted\n",
				receiver.Unknown());
		}
	}
}


static bool
ReadTrain(const char* path, Train& train)
{
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return false;
	char level;
	unsigned duration;
	while (fscanf(file, " %c %u", &level, &duration) == 2)
		train.push_back(Pulse{level == 'm', duration});
	fclose(file);
	return true;
}


/*!	A real remote, as meson_ir recorded it on the board: the JVC RM-RK52
	(9 buttons pressed once, then volume up held for 3 s).
*/
static void
ReplayCaptures()
{
#ifdef CAPTURE_DIRECTORY
	Train train;
	sChecks++;
	if (!ReadTrain(CAPTURE_DIRECTORY "/jvc-rm-rk52.txt", train)) {
		sFailures++;
		printf("FAIL capture jvc-rm-rk52.txt: can't read it\n");
		return;
	}
	Receiver receiver;
	std::vector<ir_remote_event> events = Decode(train, &receiver);
	int presses = 0;
	bool ok = events.size() == 140 && receiver.Unknown() == 0;
	for (const ir_remote_event& e : events) {
		ok = ok && e.protocol == IR_REMOTE_JVC && e.address == 0x8f;
		if ((e.flags & IR_REMOTE_REPEAT) == 0)
			presses++;
	}
	if (!ok || presses != 10) {
		sFailures++;
		printf("FAIL capture jvc-rm-rk52.txt: %zu frames, %d presses, %u "
			"unknown\n", events.size(), presses, receiver.Unknown());
	}
#endif
}


static int
DecodeFile(const char* path)
{
	Train train;
	if (!ReadTrain(path, train)) {
		perror(path);
		return 1;
	}
	Receiver receiver;
	std::vector<ir_remote_event> events = Decode(train, &receiver);
	for (const ir_remote_event& e : events) {
		printf("%s address %#x command %#x%s%s\n", Name(e.protocol), e.address,
			e.command, e.flags & IR_REMOTE_REPEAT ? " repeat" : "",
			e.flags & IR_REMOTE_TOGGLE ? " toggle" : "");
	}
	printf("%zu frames, %u unknown pulse trains\n", events.size(),
		receiver.Unknown());
	return 0;
}


int
main(int argc, char** argv)
{
	if (argc > 1)
		return DecodeFile(argv[1]);

	srand(1);
	static const int kScale[] = { 100, 95, 105 };
	static const int kStretch[] = { 0, 100, -60, 150 };
	static const int kJitter[] = { 0, 30, 60 };
	for (int scale : kScale) {
		for (int stretch : kStretch) {
			for (int jitter : kJitter) {
				sScalePercent = scale;
				sMarkStretch = stretch;
				sJitter = jitter;
				RunAll();
			}
		}
	}
	ReplayCaptures();
	printf("%d checks, %d failed\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
