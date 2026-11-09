/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * mictest [seconds] [output index]: records the default audio input through
 * BMediaRecorder as NeoCam does, and prints the format, how many buffers and
 * bytes the hook got and their level (RMS and peak, of full scale).
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <MediaRecorder.h>
#include <MediaRoster.h>


static int32 sBuffers = 0;
static int64 sBytes = 0;
static double sSquares = 0;
static double sPeak = 0;
static int64 sSamples = 0;
static bigtime_t sFirst = -1;
static bigtime_t sLast = -1;


static void
Record(void* cookie, bigtime_t timestamp, void* data, size_t size,
	const media_format& format)
{
	sBuffers++;
	sBytes += size;
	if (sFirst < 0)
		sFirst = timestamp;
	sLast = timestamp;

	uint32 sampleFormat = format.u.raw_audio.format;
	if (sampleFormat == media_raw_audio_format::B_AUDIO_FLOAT) {
		const float* samples = (const float*)data;
		for (size_t i = 0; i < size / 4; i++) {
			double value = samples[i];
			sSquares += value * value;
			if (fabs(value) > sPeak)
				sPeak = fabs(value);
		}
		sSamples += size / 4;
	} else if (sampleFormat == media_raw_audio_format::B_AUDIO_SHORT) {
		const int16* samples = (const int16*)data;
		for (size_t i = 0; i < size / 2; i++) {
			double value = samples[i] / 32768.0;
			sSquares += value * value;
			if (fabs(value) > sPeak)
				sPeak = fabs(value);
		}
		sSamples += size / 2;
	} else if (sampleFormat == media_raw_audio_format::B_AUDIO_INT) {
		const int32* samples = (const int32*)data;
		for (size_t i = 0; i < size / 4; i++) {
			double value = samples[i] / 2147483648.0;
			sSquares += value * value;
			if (fabs(value) > sPeak)
				sPeak = fabs(value);
		}
		sSamples += size / 4;
	}
}


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 3;
	int index = argc > 2 ? atoi(argv[2]) : 0;
	BApplication app("application/x-vnd.c2-mictest");

	BMediaRoster* roster = BMediaRoster::Roster();
	media_node input;
	status_t status = roster->GetAudioInput(&input);
	if (status != B_OK) {
		printf("no audio input: %s\n", strerror(status));
		return 1;
	}
	live_node_info info;
	roster->GetLiveNodeInfo(input, &info);
	printf("audio input: node %d \"%s\", kinds 0x%llx\n", (int)input.node,
		info.name, (unsigned long long)input.kind);

	media_output outputs[8];
	int32 count = 0;
	roster->GetFreeOutputsFor(input, outputs, 8, &count, B_MEDIA_RAW_AUDIO);
	for (int32 i = 0; i < count; i++) {
		const media_multi_audio_format& audio = outputs[i].format.u.raw_audio;
		printf("  output %d \"%s\": %.0f Hz, %u channels, format 0x%x, "
			"buffer %zu\n", (int)i, outputs[i].name, audio.frame_rate,
			(unsigned)audio.channel_count, (unsigned)audio.format,
			audio.buffer_size);
	}
	if (index >= count) {
		printf("no free output %d\n", index);
		return 1;
	}

	BMediaRecorder recorder("mictest", B_MEDIA_RAW_AUDIO);
	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = outputs[index].format.u.raw_audio;
	recorder.SetAcceptedFormat(format);
	status = recorder.Connect(outputs[index].node, &outputs[index], &format);
	printf("connect: %s\n", strerror(status));
	if (status != B_OK)
		return 1;
	const media_multi_audio_format& audio = recorder.Format().u.raw_audio;
	printf("connected: %.0f Hz, %u channels, format 0x%x, buffer %zu\n",
		audio.frame_rate, (unsigned)audio.channel_count,
		(unsigned)audio.format, audio.buffer_size);

	recorder.SetHooks(&Record, NULL, NULL);
	status = recorder.Start();
	printf("start: %s\n", strerror(status));
	snooze(seconds * 1000000LL);
	recorder.Stop(true);
	recorder.SetHooks(NULL, NULL, NULL);
	recorder.Disconnect();

	double rms = sSamples > 0 ? sqrt(sSquares / sSamples) : 0;
	printf("%d buffers, %lld bytes, %lld samples, times %lld to %lld us; "
		"RMS %.5f (%.1f dBFS), peak %.4f\n", (int)sBuffers, (long long)sBytes,
		(long long)sSamples, (long long)sFirst, (long long)sLast, rms,
		rms > 0 ? 20 * log10(rms) : -999.0, sPeak);
	roster->ReleaseNode(input);
	return sBuffers > 0 ? 0 : 2;
}
