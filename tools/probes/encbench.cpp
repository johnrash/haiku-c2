/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * encbench [<codec> <width> <height> <frames> [<quality> [<Mbit/s>]]]: how fast the
 * Media Kit's encoders write B_RGB32 video into Matroska, for choosing what
 * NeoCam records at which sizes. Without arguments it lists the encoders
 * that take B_RGB32 for Matroska. The frames are a moving gradient with
 * some noise, closer to a camera's than a flat picture; the file is
 * /boot/home/encbench.mkv. Prints frames a second, CPU time a frame and
 * the file's size. With a bit rate (at 30 frames a second), the encoder is
 * asked for it (encode_parameters' avg_field_size), and the rate written
 * is printed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Entry.h>
#include <File.h>
#include <MediaDefs.h>
#include <MediaFile.h>
#include <MediaFormats.h>
#include <MediaTrack.h>
#include <OS.h>


static const char* kPath = "/boot/home/encbench.mkv";


static bool
find_matroska(media_file_format* fileFormat)
{
	int32 cookie = 0;
	while (get_next_file_format(&cookie, fileFormat) == B_OK) {
		if ((fileFormat->capabilities & media_file_format::B_WRITABLE) != 0
			&& strcmp(fileFormat->mime_type, "video/x-matroska") == 0)
			return true;
	}
	return false;
}


static media_format
raw_format(uint32 width, uint32 height)
{
	media_format format;
	format.type = B_MEDIA_RAW_VIDEO;
	format.u.raw_video = media_raw_video_format::wildcard;
	format.u.raw_video.field_rate = 30;
	format.u.raw_video.interlace = 1;
	format.u.raw_video.pixel_width_aspect = 1;
	format.u.raw_video.pixel_height_aspect = 1;
	format.u.raw_video.display.format = B_RGB32;
	format.u.raw_video.display.line_width = width;
	format.u.raw_video.display.line_count = height;
	format.u.raw_video.display.bytes_per_row = width * 4;
	return format;
}


static void
fill(uint32* bits, uint32 width, uint32 height, int frame)
{
	uint32 seed = frame * 2654435761u;
	for (uint32 y = 0; y < height; y++) {
		for (uint32 x = 0; x < width; x++) {
			seed = seed * 1103515245 + 12345;
			uint32 noise = (seed >> 16) & 7;
			uint32 r = ((x + frame * 4) * 255 / width + noise) & 0xff;
			uint32 g = ((y + frame * 2) * 255 / height + noise) & 0xff;
			uint32 b = ((x + y) * 128 / (width + height) + noise) & 0xff;
			bits[y * width + x] = 0xff000000 | (r << 16) | (g << 8) | b;
		}
	}
}


int
main(int argc, char** argv)
{
	media_file_format fileFormat;
	if (!find_matroska(&fileFormat)) {
		fprintf(stderr, "no Matroska writer\n");
		return 1;
	}

	if (argc < 5) {
		media_format inFormat = raw_format(640, 480);
		media_format outFormat;
		media_codec_info codec;
		int32 cookie = 0;
		while (get_next_encoder(&cookie, &fileFormat, &inFormat, &outFormat,
				&codec) == B_OK) {
			printf("%-12s %s\n", codec.short_name, codec.pretty_name);
		}
		return 0;
	}

	const char* codecName = argv[1];
	uint32 width = atoi(argv[2]);
	uint32 height = atoi(argv[3]);
	int frames = atoi(argv[4]);
	float quality = argc > 5 ? atof(argv[5]) : -1;
	float megabits = argc > 6 ? atof(argv[6]) : 0;

	media_format inFormat = raw_format(width, height);
	media_format outFormat;
	media_codec_info codec;
	int32 cookie = 0;
	bool found = false;
	while (get_next_encoder(&cookie, &fileFormat, &inFormat, &outFormat,
			&codec) == B_OK) {
		if (strcmp(codec.short_name, codecName) == 0) {
			found = true;
			break;
		}
	}
	if (!found) {
		fprintf(stderr, "no encoder %s\n", codecName);
		return 1;
	}

	entry_ref ref;
	BEntry(kPath).GetRef(&ref);
	BFile(kPath, B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	BMediaFile file(&ref, &fileFormat);
	if (file.InitCheck() != B_OK) {
		fprintf(stderr, "BMediaFile: %s\n", strerror(file.InitCheck()));
		return 1;
	}
	BMediaTrack* track = file.CreateTrack(&inFormat, &codec);
	if (track == NULL) {
		fprintf(stderr, "CreateTrack failed\n");
		return 1;
	}
	if (quality >= 0)
		track->SetQuality(quality);
	if (megabits > 0) {
		encode_parameters parameters;
		memset(&parameters, 0, sizeof(parameters));
		track->GetEncodeParameters(&parameters);
		parameters.avg_field_size = (int32)(megabits * 1e6 / 8 / 30);
		status_t status = track->SetEncodeParameters(&parameters);
		if (status != B_OK)
			fprintf(stderr, "SetEncodeParameters: %s\n", strerror(status));
	}
	file.CommitHeader();

	// a few different frames, used in turn: filling isn't what is measured
	const int kPatterns = 8;
	uint32* bits[kPatterns];
	for (int i = 0; i < kPatterns; i++) {
		bits[i] = (uint32*)malloc(width * height * 4);
		fill(bits[i], width, height, i);
	}

	thread_info info;
	get_thread_info(find_thread(NULL), &info);
	bigtime_t cpuStart = info.user_time + info.kernel_time;
	bigtime_t start = system_time();
	for (int i = 0; i < frames; i++) {
		status_t status = track->WriteFrames(bits[i % kPatterns], 1,
			i == 0 ? B_MEDIA_KEY_FRAME : 0);
		if (status != B_OK) {
			fprintf(stderr, "WriteFrames: %s\n", strerror(status));
			return 1;
		}
	}
	file.CloseFile();
	bigtime_t elapsed = system_time() - start;

	// the team's CPU, for encoders with threads of their own
	team_usage_info usage;
	get_team_usage_info(B_CURRENT_TEAM, B_TEAM_USAGE_SELF, &usage);
	bigtime_t cpu = usage.user_time + usage.kernel_time - cpuStart;

	off_t size = 0;
	BFile(kPath, B_READ_ONLY).GetSize(&size);
	printf("%s %ux%u: %d frames in %.2f s, %.1f frames/s, %.1f ms CPU a "
		"frame, %.1f KiB a frame\n", codecName, width, height, frames,
		elapsed / 1e6, frames * 1e6 / elapsed, cpu / 1e3 / frames,
		size / 1024.0 / frames);
	printf("  %.2f Mbit/s at 30 frames a second\n",
		size * 8.0 * 30 / frames / 1e6);
	return 0;
}
