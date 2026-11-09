/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * aactest <file.mkv> [seconds] [video codec]: writes a Matroska file as
 * NeoCam's VideoRecorder does, a 320x240 B_RGB32 video track (h264, the
 * hardware's, or another encoder's short name; "none" for sound only) and a
 * 16 kHz mono 16-bit sound track ("aac", else "flac") of a 440 Hz tone,
 * one thread each. Prints each step's status and the frames that failed.
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Entry.h>
#include <MediaFile.h>
#include <MediaFormats.h>
#include <MediaTrack.h>
#include <OS.h>


static const uint32 kWidth = 320;
static const uint32 kHeight = 240;
static const float kFrameRate = 30;
static const float kSampleRate = 16000;
static const int32 kSamplesPerBuffer = 2048;

static BMediaTrack* sAudioTrack;
static int sSeconds;
static int32 sAudioFailures;
static status_t sAudioFirstError;


static BMediaTrack*
create_track(BMediaFile& file, media_file_format& fileFormat,
	media_format& inFormat, const char* name)
{
	media_format outFormat;
	media_codec_info codec;
	int32 cookie = 0;
	while (get_next_encoder(&cookie, &fileFormat, &inFormat, &outFormat,
			&codec) == B_OK) {
		if (strcmp(codec.short_name, name) != 0)
			continue;
		BMediaTrack* track = file.CreateTrack(&inFormat, &codec);
		printf("track \"%s\" (%s): %s, InitCheck %s\n", codec.short_name,
			codec.pretty_name, track != NULL ? "created" : "not created",
			track != NULL ? strerror(track->InitCheck()) : "-");
		if (track != NULL && track->InitCheck() != B_OK) {
			file.ReleaseTrack(track);
			return NULL;
		}
		return track;
	}
	printf("no \"%s\" encoder for the file format\n", name);
	return NULL;
}


static status_t
audio_thread(void*)
{
	int16 samples[kSamplesPerBuffer];
	int64 written = 0;
	bigtime_t start = system_time();
	while (written < sSeconds * kSampleRate) {
		for (int32 i = 0; i < kSamplesPerBuffer; i++) {
			samples[i] = (int16)(8000 * sin(2 * M_PI * 440
				* (written + i) / kSampleRate));
		}
		status_t status = sAudioTrack->WriteFrames(samples,
			kSamplesPerBuffer);
		if (status != B_OK) {
			if (sAudioFailures++ == 0)
				sAudioFirstError = status;
		}
		written += kSamplesPerBuffer;
		// in real time, as a microphone's buffers come
		snooze_until(start + (bigtime_t)(written * 1000000 / kSampleRate),
			B_SYSTEM_TIMEBASE);
	}
	return B_OK;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: aactest <file.mkv> [seconds] [video codec]\n");
		return 1;
	}
	sSeconds = argc > 2 ? atoi(argv[2]) : 3;
	const char* videoCodec = argc > 3 ? argv[3] : "h264";

	media_file_format fileFormat;
	int32 cookie = 0;
	bool found = false;
	while (get_next_file_format(&cookie, &fileFormat) == B_OK) {
		if ((fileFormat.capabilities & media_file_format::B_WRITABLE) != 0
			&& strcmp(fileFormat.mime_type, "video/x-matroska") == 0) {
			found = true;
			break;
		}
	}
	if (!found) {
		printf("no Matroska writer\n");
		return 1;
	}

	entry_ref ref;
	get_ref_for_path(argv[1], &ref);
	BMediaFile file(&ref, &fileFormat);
	printf("file: %s\n", strerror(file.InitCheck()));

	BMediaTrack* videoTrack = NULL;
	if (strcmp(videoCodec, "none") != 0) {
		media_format video;
		video.type = B_MEDIA_RAW_VIDEO;
		video.u.raw_video = media_raw_video_format::wildcard;
		video.u.raw_video.field_rate = kFrameRate;
		video.u.raw_video.interlace = 1;
		video.u.raw_video.pixel_width_aspect = 1;
		video.u.raw_video.pixel_height_aspect = 1;
		video.u.raw_video.display.format = B_RGB32;
		video.u.raw_video.display.line_width = kWidth;
		video.u.raw_video.display.line_count = kHeight;
		video.u.raw_video.display.bytes_per_row = kWidth * 4;
		videoTrack = create_track(file, fileFormat, video, videoCodec);
		if (videoTrack == NULL)
			return 1;
	}

	media_format audio;
	audio.type = B_MEDIA_RAW_AUDIO;
	audio.u.raw_audio = media_multi_audio_format::wildcard;
	audio.u.raw_audio.frame_rate = kSampleRate;
	audio.u.raw_audio.channel_count = 1;
	audio.u.raw_audio.format = media_raw_audio_format::B_AUDIO_SHORT;
	audio.u.raw_audio.byte_order = B_MEDIA_LITTLE_ENDIAN;
	audio.u.raw_audio.buffer_size = kSamplesPerBuffer * 2;
	sAudioTrack = create_track(file, fileFormat, audio, "aac");
	if (sAudioTrack == NULL)
		sAudioTrack = create_track(file, fileFormat, audio, "flac");
	if (sAudioTrack == NULL)
		return 1;

	printf("header: %s\n", strerror(file.CommitHeader()));

	thread_id thread = spawn_thread(audio_thread, "aactest audio",
		B_NORMAL_PRIORITY, NULL);
	resume_thread(thread);

	int32 videoFailures = 0;
	status_t videoFirstError = B_OK;
	if (videoTrack != NULL) {
		uint32* frame = (uint32*)malloc(kWidth * kHeight * 4);
		bigtime_t start = system_time();
		for (int32 i = 0; i < sSeconds * kFrameRate; i++) {
			// a bar that moves
			for (uint32 y = 0; y < kHeight; y++) {
				for (uint32 x = 0; x < kWidth; x++) {
					frame[y * kWidth + x] = (x + i * 4) % kWidth < 40
						? 0xffffffff : 0xff204060;
				}
			}
			status_t status = videoTrack->WriteFrames(frame, 1,
				i % 60 == 0 ? B_MEDIA_KEY_FRAME : 0);
			if (status != B_OK && videoFailures++ == 0)
				videoFirstError = status;
			snooze_until(start + (bigtime_t)((i + 1) * 1000000 / kFrameRate),
				B_SYSTEM_TIMEBASE);
		}
		free(frame);
	}

	status_t result;
	wait_for_thread(thread, &result);
	printf("video: %d frames failed (first: %s); sound: %d buffers failed "
		"(first: %s)\n", (int)videoFailures, strerror(videoFirstError),
		(int)sAudioFailures, strerror(sAudioFirstError));

	if (videoTrack != NULL)
		videoTrack->Flush();
	sAudioTrack->Flush();
	printf("close: %s\n", strerror(file.CloseFile()));
	return 0;
}
