/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * Decodes a file's video track through the Media Kit, as MediaPlayer does,
 * and reports which decoder the Media Kit chose (meson_decoder, the S905's
 * video decoder, or FFmpeg's), the frame rate and whether the frames' times
 * rise steadily (milestone M26).
 *
 *   mediadecode <file> [rgb|422|nv12] [<output>]
 *   mediadecode <file> seek
 *
 * 422 asks for B_YCbCr422, nv12 for B_NV12 (what the overlay shows), rgb
 * (the default) for B_RGB32. With <output>, the frames' Y values (422), the
 * frames as NV12 without padding (nv12, for tools/vdec-compare.py) or the
 * frames (rgb) are written to it, for comparing with FFmpeg's decoding.
 * seek decodes ten frames at a few places in turn, timing the first frame
 * after each seek.
 *
 * Build: $CXX -O2 -o mediadecode mediadecode.cpp -lbe -lmedia
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Entry.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <OS.h>


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr,
			"usage: mediadecode <file> [rgb|422|nv12] [<output>]\n");
		return 1;
	}
	bool ycbcr = argc > 2 && strcmp(argv[2], "422") == 0;
	bool nv12 = argc > 2 && strcmp(argv[2], "nv12") == 0;
	bool seek = argc > 2 && strcmp(argv[2], "seek") == 0;
	FILE* output = NULL;
	if (argc > 3 && (output = fopen(argv[3], "wb")) == NULL) {
		perror(argv[3]);
		return 1;
	}

	entry_ref ref;
	if (get_ref_for_path(argv[1], &ref) != B_OK) {
		fprintf(stderr, "mediadecode: %s: not found\n", argv[1]);
		return 1;
	}
	bigtime_t begin = system_time();
	BMediaFile file(&ref);
	if (file.InitCheck() != B_OK) {
		fprintf(stderr, "mediadecode: %s: %s\n", argv[1],
			strerror(file.InitCheck()));
		return 1;
	}

	BMediaTrack* track = NULL;
	for (int32 i = 0; i < file.CountTracks(); i++) {
		BMediaTrack* candidate = file.TrackAt(i);
		media_format format;
		if (candidate != NULL && candidate->EncodedFormat(&format) == B_OK
			&& format.type == B_MEDIA_ENCODED_VIDEO) {
			track = candidate;
			break;
		}
		file.ReleaseTrack(candidate);
	}
	if (track == NULL) {
		fprintf(stderr, "mediadecode: no video track\n");
		return 1;
	}

	media_codec_info codec;
	if (track->GetCodecInfo(&codec) == B_OK)
		printf("decoder: %s (%s)\n", codec.pretty_name, codec.short_name);

	media_format format;
	format.type = B_MEDIA_RAW_VIDEO;
	format.u.raw_video = media_raw_video_format::wildcard;
	format.u.raw_video.display.format
		= nv12 ? B_NV12 : ycbcr ? B_YCbCr422 : B_RGB32;
	status_t status = track->DecodedFormat(&format);
	if (status != B_OK) {
		fprintf(stderr, "mediadecode: DecodedFormat: %s\n", strerror(status));
		return 1;
	}
	const media_video_display_info& display = format.u.raw_video.display;
	printf("format: %ux%u, %s, %u bytes per row, %.2f frames/s; set up in "
		"%lld ms\n", (unsigned)display.line_width,
		(unsigned)display.line_count,
		display.format == B_YCbCr422 ? "B_YCbCr422"
			: display.format == B_NV12 ? "B_NV12"
			: display.format == B_RGB32 ? "B_RGB32" : "other",
		(unsigned)display.bytes_per_row, format.u.raw_video.field_rate,
		(long long)(system_time() - begin) / 1000);

	// B_NV12: the CbCr plane follows the Y plane
	uint32 chromaLines = display.format == B_NV12
		? (display.line_count + 1) / 2 : 0;
	size_t size = (size_t)display.bytes_per_row
		* (display.line_count + chromaLines);
	uint8* buffer = (uint8*)malloc(size);

	if (seek) {
		static const bigtime_t kPlaces[] = { 10000000, 2000000, 25000000,
			500000, 0, 15000000 };
		for (size_t i = 0; i < sizeof(kPlaces) / sizeof(kPlaces[0]); i++) {
			bigtime_t time = kPlaces[i];
			bigtime_t before = system_time();
			status = track->SeekToTime(&time, B_MEDIA_SEEK_CLOSEST_BACKWARD);
			bigtime_t first = 0;
			bigtime_t firstTime = -1;
			bigtime_t lastTime = -1;
			int32 count = 0;
			for (; status == B_OK && count < 10; count++) {
				int64 frameCount = 0;
				media_header header;
				status = track->ReadFrames(buffer, &frameCount, &header);
				if (status != B_OK)
					break;
				if (count == 0) {
					first = system_time() - before;
					firstTime = header.start_time;
				} else if (header.start_time <= lastTime)
					printf("  frame %d: time %lld after %lld\n", (int)count,
						(long long)header.start_time, (long long)lastTime);
				lastTime = header.start_time;
			}
			printf("seek to %lld us (keyframe at %lld): first frame at %lld "
				"us after %lld ms; %d frames to %lld us%s%s\n",
				(long long)kPlaces[i], (long long)time, (long long)firstTime,
				(long long)first / 1000, (int)count, (long long)lastTime,
				status == B_OK ? "" : ", ", status == B_OK ? ""
					: strerror(status));
		}
		file.ReleaseTrack(track);
		return 0;
	}
	uint8* luma = (uint8*)malloc(display.line_width);
	int64 frames = 0;
	bigtime_t lastTime = -1;
	bigtime_t step = -1;
	int64 uneven = 0;
	begin = system_time();
	while (true) {
		int64 count = 0;
		media_header header;
		status = track->ReadFrames(buffer, &count, &header);
		if (status != B_OK)
			break;
		if (lastTime >= 0) {
			bigtime_t delta = header.start_time - lastTime;
			if (step < 0)
				step = delta;
			else if (delta < step - 1000 || delta > step + 1000) {
				if (uneven < 5) {
					printf("frame %lld: time %lld, %lld after the last\n",
						(long long)frames, (long long)header.start_time,
						(long long)delta);
				}
				uneven++;
			}
		}
		lastTime = header.start_time;
		if (output != NULL && display.format == B_NV12) {
			for (uint32 y = 0; y < display.line_count + chromaLines; y++) {
				fwrite(buffer + (size_t)y * display.bytes_per_row, 1,
					display.line_width, output);
			}
		} else if (output != NULL) {
			for (uint32 y = 0; y < display.line_count; y++) {
				const uint8* row = buffer + (size_t)y * display.bytes_per_row;
				if (ycbcr) {
					for (uint32 x = 0; x < display.line_width; x++)
						luma[x] = row[2 * x];
					fwrite(luma, 1, display.line_width, output);
				} else
					fwrite(row, 4, display.line_width, output);
			}
		}
		frames++;
	}
	bigtime_t took = system_time() - begin;
	printf("%lld frames in %lld ms: %.1f frames/s (%s); %lld uneven "
		"steps, last time %lld us\n", (long long)frames,
		(long long)took / 1000, frames * 1000000.0 / took,
		status == B_LAST_BUFFER_ERROR ? "end" : strerror(status),
		(long long)uneven, (long long)lastTime);
	if (output != NULL)
		fclose(output);
	file.ReleaseTrack(track);
	return frames > 0 ? 0 : 1;
}
