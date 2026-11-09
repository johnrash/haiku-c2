/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * audiotest <device> [<seconds> [<tone Hz> [<rate Hz> [<late ms>]]]]: a
 * multi_audio
 * driver (e.g.
 * /dev/audio/hmulti/usb/1) without the media_server, which must not have it
 * open (stop media_server and media_addon_server first). Plays a sine tone
 * (default 1000 Hz, a quarter of full scale; 0 plays silence) on all
 * playback channels, records from the inputs, and prints per second how
 * many frames each direction moved, the input level, the input's frequency
 * (from its zero crossings: a cable from the output to the input gives the
 * tone back) and the spread of the intervals between the driver's playback
 * buffer times (what the media node's time source is computed from). A USB
 * audio device that keeps up
 * shows its sample rate on both. With a rate the device is set to it first
 * (B_MULTI_SET_GLOBAL_FORMAT); the tone is moved to the nearest frequency
 * with a whole number of periods per buffer.
 * The recorded buffer read is the one the driver names (record_buffer_cycle),
 * as the media node reads it; the node skips one named twice in a row.
 * Counted: such repeats, and jumps in the order. With <late ms>, every 2 s
 * the test calls the driver that much later, as a busy media node would.
 * With AUDIOTEST_DUMP=<file> in the environment the first input channel goes
 * there as raw 16-bit samples, for a look at the waveform on another machine.
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>

#include <hmulti_audio.h>


static const int kMaxBuffers = 8;
static const int kMaxChannels = 8;


int
main(int argc, char** argv)
{
	if (argc < 2) {
		printf("usage: audiotest <device> [<seconds> [<tone Hz> [<rate Hz> "
			"[<late ms>]]]]\n");
		return 1;
	}
	int seconds = argc > 2 ? atoi(argv[2]) : 10;
	double tone = argc > 3 ? atof(argv[3]) : 1000;
	double rate = argc > 4 ? atof(argv[4]) : 48000;
	bigtime_t late = argc > 5 ? atoi(argv[5]) * 1000LL : 0;
	static const struct { uint32 rate; uint32 code; } kRates[] = {
		{ 8000, B_SR_8000 }, { 11025, B_SR_11025 }, { 12000, B_SR_12000 },
		{ 16000, B_SR_16000 }, { 22050, B_SR_22050 }, { 24000, B_SR_24000 },
		{ 32000, B_SR_32000 }, { 44100, B_SR_44100 }, { 48000, B_SR_48000 },
		{ 64000, B_SR_64000 }, { 88200, B_SR_88200 }, { 96000, B_SR_96000 },
		{ 176400, B_SR_176400 }, { 192000, B_SR_192000 },
	};
	uint32 rateCode = 0;
	for (size_t i = 0; i < sizeof(kRates) / sizeof(kRates[0]); i++) {
		if (kRates[i].rate == (uint32)rate)
			rateCode = kRates[i].code;
	}
	if (argc > 4 && rateCode == 0 && rate != 0) {
		printf("audiotest: no multi_audio code for %g Hz\n", rate);
		return 1;
	}

	int fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		printf("audiotest: %s: %s\n", argv[1], strerror(errno));
		return 1;
	}

	multi_channel_info channels[16];
	multi_description description;
	memset(&description, 0, sizeof(description));
	description.info_size = sizeof(description);
	description.request_channel_count = 16;
	description.channels = channels;
	if (ioctl(fd, B_MULTI_GET_DESCRIPTION, &description,
			sizeof(description)) != 0) {
		printf("audiotest: B_MULTI_GET_DESCRIPTION: %s\n", strerror(errno));
		return 1;
	}
	printf("%s (%s): %d out, %d in\n", description.friendly_name,
		description.vendor_info, (int)description.output_channel_count,
		(int)description.input_channel_count);

	multi_format_info format;
	memset(&format, 0, sizeof(format));
	format.info_size = sizeof(format);
	if (argc > 4 && rateCode != 0
		&& ioctl(fd, B_MULTI_GET_GLOBAL_FORMAT, &format, sizeof(format)) == 0) {
		format.output.rate = rateCode;
		format.input.rate = rateCode;
		if (ioctl(fd, B_MULTI_SET_GLOBAL_FORMAT, &format, sizeof(format)) != 0) {
			printf("audiotest: B_MULTI_SET_GLOBAL_FORMAT %g Hz: %s\n", rate,
				strerror(errno));
			return 1;
		}
	}
	memset(&format, 0, sizeof(format));
	format.info_size = sizeof(format);
	if (ioctl(fd, B_MULTI_GET_GLOBAL_FORMAT, &format, sizeof(format)) == 0) {
		printf("format: output rate %#x format %#x, input rate %#x format"
			" %#x\n", (unsigned)format.output.rate,
			(unsigned)format.output.format, (unsigned)format.input.rate,
			(unsigned)format.input.format);
		// the input's own rate, for its frequency (an input-only device
		// such as a webcam's microphone has no output rate)
		for (size_t i = 0; i < sizeof(kRates) / sizeof(kRates[0]); i++) {
			if (kRates[i].code == format.input.rate)
				rate = kRates[i].rate;
		}
	}

	buffer_desc playbackDescs[kMaxBuffers][kMaxChannels];
	buffer_desc recordDescs[kMaxBuffers][kMaxChannels];
	buffer_desc* playback[kMaxBuffers];
	buffer_desc* record[kMaxBuffers];
	for (int i = 0; i < kMaxBuffers; i++) {
		playback[i] = playbackDescs[i];
		record[i] = recordDescs[i];
	}
	multi_buffer_list list;
	memset(&list, 0, sizeof(list));
	list.info_size = sizeof(list);
	list.request_playback_buffers = kMaxBuffers;
	list.request_playback_channels = kMaxChannels;
	list.playback_buffers = playback;
	list.request_record_buffers = kMaxBuffers;
	list.request_record_channels = kMaxChannels;
	list.record_buffers = record;
	if (ioctl(fd, B_MULTI_GET_BUFFERS, &list, sizeof(list)) != 0) {
		printf("audiotest: B_MULTI_GET_BUFFERS: %s\n", strerror(errno));
		return 1;
	}
	int playChannels = list.return_playback_channels;
	int recordChannels = list.return_record_channels;
	uint32 playFrames = list.return_playback_buffer_size;
	uint32 recordFrames = list.return_record_buffer_size;
	printf("buffers: playback %d x %u frames x %d channels, record %d x %u"
		" frames x %d channels\n", (int)list.return_playback_buffers,
		(unsigned)playFrames, playChannels, (int)list.return_record_buffers,
		(unsigned)recordFrames, recordChannels);

	// 16-bit samples (the USB audio class format of our devices); the tone
	// is written into every playback buffer once: with a whole number of
	// periods per buffer it is continuous in whichever order they are sent
	if (tone > 0 && playFrames > 0) {
		double periods = floor(tone * playFrames / rate + 0.5);
		if (periods < 1)
			periods = 1;
		tone = periods * rate / playFrames;
		printf("tone %.3f Hz at %g Hz (%.0f periods per buffer)\n", tone, rate,
			periods);
	}
	for (int b = 0; b < list.return_playback_buffers; b++) {
		for (int c = 0; c < playChannels && c < kMaxChannels; c++) {
			for (uint32 f = 0; f < playFrames; f++) {
				int16* sample = (int16*)(playback[b][c].base
					+ f * playback[b][c].stride);
				*sample = tone > 0
					? (int16)(8192 * sin(2 * M_PI * tone * f / rate)) : 0;
			}
		}
	}

	multi_buffer_info info;
	memset(&info, 0, sizeof(info));
	info.info_size = sizeof(info);
	bigtime_t start = system_time();
	bigtime_t lastReport = start;
	bigtime_t lastPlayed = 0, lastRecorded = 0;
	bigtime_t lastPlayedTime = 0, minInterval = 1000000, maxInterval = 0;
	double sumSquares = 0;
	int peak = 0;
	uint64 samples = 0;
	// zero crossings, with some hysteresis against noise
	static const int kHysteresis = 256;
	int sign = 0;
	uint64 crossings = 0;
	int exchanges = 0;
	int32 lastCycle = -1;
	int repeats = 0, jumps = 0, recordedBuffers = 0;
	bigtime_t nextLate = start + 2000000;
	FILE* dump = getenv("AUDIOTEST_DUMP") != NULL
		? fopen(getenv("AUDIOTEST_DUMP"), "wb") : NULL;
	while (system_time() - start < seconds * 1000000LL) {
		if (late > 0 && system_time() >= nextLate) {
			snooze(late);
			nextLate += 2000000;
		}
		bigtime_t recordedBefore = info.recorded_frames_count;
		if (ioctl(fd, B_MULTI_BUFFER_EXCHANGE, &info, sizeof(info)) != 0) {
			printf("audiotest: B_MULTI_BUFFER_EXCHANGE: %s\n",
				strerror(errno));
			break;
		}
		exchanges++;
		if (info.played_real_time != lastPlayedTime) {
			if (lastPlayedTime != 0 && exchanges > 8) {
				bigtime_t interval = info.played_real_time - lastPlayedTime;
				if (interval < minInterval)
					minInterval = interval;
				if (interval > maxInterval)
					maxInterval = interval;
			}
			lastPlayedTime = info.played_real_time;
		}
		if (info.recorded_frames_count != recordedBefore && recordChannels > 0) {
			// the buffer the driver names, as the media node reads it
			int b = info.record_buffer_cycle;
			recordedBuffers++;
			if (lastCycle >= 0) {
				if (b == lastCycle)
					repeats++;
				else if (b != (lastCycle + 1) % list.return_record_buffers)
					jumps++;
			}
			lastCycle = b;
			for (uint32 f = 0; f < recordFrames; f++) {
				int16 sample = *(int16*)(record[b][0].base
					+ f * record[b][0].stride);
				if (dump != NULL)
					fwrite(&sample, sizeof(sample), 1, dump);
				sumSquares += (double)sample * sample;
				if (abs(sample) > peak)
					peak = abs(sample);
				samples++;
				if (sample > kHysteresis && sign <= 0) {
					if (sign < 0)
						crossings++;
					sign = 1;
				} else if (sample < -kHysteresis && sign >= 0) {
					if (sign > 0)
						crossings++;
					sign = -1;
				}
			}
		}

		bigtime_t now = system_time();
		if (now - lastReport >= 1000000) {
			double elapsed = (now - lastReport) / 1000000.0;
			printf("%4.1f s: played %6.0f frames/s, recorded %6.0f frames/s,"
				" input rms %.0f peak %d %.0f Hz, buffer interval %.1f-%.1f"
				" ms\n", (now - start) / 1000000.0,
				(info.played_frames_count - lastPlayed) / elapsed,
				(info.recorded_frames_count - lastRecorded) / elapsed,
				samples > 0 ? sqrt(sumSquares / samples) : 0.0, peak,
				samples > 0 ? crossings / 2.0 * rate / samples : 0.0,
				minInterval / 1000.0, maxInterval / 1000.0);
			minInterval = 1000000;
			maxInterval = 0;
			lastPlayed = info.played_frames_count;
			lastRecorded = info.recorded_frames_count;
			lastReport = now;
			sumSquares = 0;
			peak = 0;
			samples = 0;
			crossings = 0;
		}
	}
	ioctl(fd, B_MULTI_BUFFER_FORCE_STOP, NULL, 0);
	if (dump != NULL)
		fclose(dump);
	printf("%d exchanges, %lld frames played, %lld recorded in %.1f s\n",
		exchanges, (long long)info.played_frames_count,
		(long long)info.recorded_frames_count,
		(system_time() - start) / 1000000.0);
	printf("recorded buffers %d: %d named twice in a row (the node skips"
		" them), %d out of order\n", recordedBuffers, repeats, jumps);
	close(fd);
	return 0;
}
