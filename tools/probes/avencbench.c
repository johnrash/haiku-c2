/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * avencbench <codec> <width> <height> <frames> <threads> [neon]: FFmpeg's
 * encoder as the Media Kit's FFmpeg plugin drives it (BGRA converted with
 * swscale's fast bilinear, or with "neon" the plugin's own converter, then
 * encoded), without the plugin: the time of each part, for one thread or
 * slice threads. With "neon", the first frame is also converted both ways
 * and the largest difference per plane printed. Frames as encbench's.
 * Build: $CC -O2 $HAIKU_CPPFLAGS -I<ffmpeg6>/develop/headers
 * -I<ffmpeg6>/develop/headers/libavcodec -I<haiku>/src/add-ons/media/plugins/
 * ffmpeg -L<ffmpeg6>/develop/lib -o avencbench avencbench.c
 * <haiku>/src/add-ons/media/plugins/ffmpeg/gfx_conv_neon.c -lavcodec
 * -lswscale -lavutil
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <OS.h>

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "gfx_conv_neon.h"


static void
fill(uint32_t* bits, int width, int height, int frame)
{
	uint32_t seed = frame * 2654435761u;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			seed = seed * 1103515245 + 12345;
			uint32_t noise = (seed >> 16) & 7;
			uint32_t r = ((x + frame * 4) * 255 / width + noise) & 0xff;
			uint32_t g = ((y + frame * 2) * 255 / height + noise) & 0xff;
			uint32_t b = ((x + y) * 128 / (width + height) + noise) & 0xff;
			bits[y * width + x] = 0xff000000 | (r << 16) | (g << 8) | b;
		}
	}
}


int
main(int argc, char** argv)
{
	if (argc < 6) {
		fprintf(stderr, "usage: avencbench <codec> <width> <height> <frames> "
			"<threads>\n");
		return 2;
	}
	int width = atoi(argv[2]);
	int height = atoi(argv[3]);
	int frames = atoi(argv[4]);
	int threads = atoi(argv[5]);
	int neon = argc > 6 && strcmp(argv[6], "neon") == 0;

	const AVCodec* codec = avcodec_find_encoder_by_name(argv[1]);
	if (codec == NULL) {
		fprintf(stderr, "no encoder %s\n", argv[1]);
		return 1;
	}
	AVCodecContext* context = avcodec_alloc_context3(codec);
	context->width = width;
	context->height = height;
	context->time_base = (AVRational){1, 30};
	context->framerate = (AVRational){30, 1};
	context->gop_size = 12;
	context->bit_rate = 4000000;
	context->pix_fmt = codec->pix_fmts[0];
	for (int i = 0; codec->pix_fmts[i] != AV_PIX_FMT_NONE; i++)
		context->pix_fmt = codec->pix_fmts[i];
	context->thread_count = threads;
	context->thread_type = FF_THREAD_SLICE;
	if (avcodec_open2(context, codec, NULL) < 0) {
		fprintf(stderr, "avcodec_open2 failed\n");
		return 1;
	}

	struct SwsContext* sws = sws_getContext(width, height, AV_PIX_FMT_BGRA,
		width, height, context->pix_fmt, SWS_FAST_BILINEAR, NULL, NULL, NULL);
	AVFrame* frame = av_frame_alloc();
	frame->format = context->pix_fmt;
	frame->width = width;
	frame->height = height;
	av_frame_get_buffer(frame, 0);
	AVPacket* packet = av_packet_alloc();

	const int kPatterns = 8;
	uint32_t* bits[kPatterns];
	for (int i = 0; i < kPatterns; i++) {
		bits[i] = malloc(width * height * 4);
		fill(bits[i], width, height, i);
	}

	if (neon) {
		AVFrame* reference = av_frame_alloc();
		reference->format = context->pix_fmt;
		reference->width = width;
		reference->height = height;
		av_frame_get_buffer(reference, 0);
		const uint8_t* source[1] = { (const uint8_t*)bits[3] };
		int sourceStride[1] = { width * 4 };
		sws_scale(sws, source, sourceStride, 0, height, reference->data,
			reference->linesize);
		av_frame_make_writable(frame);
		gfx_conv_rgb32_yuv420p_neon((const uint8_t*)bits[3], width * 4, frame,
			width, height);
		for (int plane = 0; plane < 3; plane++) {
			int planeWidth = plane == 0 ? width : (width + 1) / 2;
			int planeHeight = plane == 0 ? height : (height + 1) / 2;
			int largest = 0;
			long total = 0;
			for (int y = 0; y < planeHeight; y++) {
				for (int x = 0; x < planeWidth; x++) {
					int a = frame->data[plane][y * frame->linesize[plane] + x];
					int b = reference->data[plane][
						y * reference->linesize[plane] + x];
					int difference = a > b ? a - b : b - a;
					if (difference > largest)
						largest = difference;
					total += difference;
				}
			}
			printf("plane %d: largest difference from swscale %d, average "
				"%.3f\n", plane, largest,
				(double)total / planeWidth / planeHeight);
		}
		av_frame_free(&reference);
	}

	team_usage_info usage;
	get_team_usage_info(B_CURRENT_TEAM, B_TEAM_USAGE_SELF, &usage);
	bigtime_t cpuStart = usage.user_time + usage.kernel_time;
	bigtime_t convertTime = 0;
	bigtime_t start = system_time();
	size_t bytes = 0;
	for (int i = 0; i < frames; i++) {
		bigtime_t convertStart = system_time();
		const uint8_t* source[1] = { (const uint8_t*)bits[i % kPatterns] };
		int sourceStride[1] = { width * 4 };
		av_frame_make_writable(frame);
		if (neon) {
			gfx_conv_rgb32_yuv420p_neon(source[0], sourceStride[0], frame,
				width, height);
		} else {
			sws_scale(sws, source, sourceStride, 0, height, frame->data,
				frame->linesize);
		}
		convertTime += system_time() - convertStart;
		frame->pts = i;
		avcodec_send_frame(context, frame);
		while (avcodec_receive_packet(context, packet) == 0) {
			bytes += packet->size;
			av_packet_unref(packet);
		}
	}
	avcodec_send_frame(context, NULL);
	while (avcodec_receive_packet(context, packet) == 0) {
		bytes += packet->size;
		av_packet_unref(packet);
	}
	bigtime_t elapsed = system_time() - start;
	get_team_usage_info(B_CURRENT_TEAM, B_TEAM_USAGE_SELF, &usage);
	bigtime_t cpu = usage.user_time + usage.kernel_time - cpuStart;

	printf("%s %dx%d, %d thread(s)%s: %.1f frames/s, conversion %.1f ms a "
		"frame, CPU %.1f ms a frame, %.1f KiB a frame\n", argv[1], width,
		height, threads, neon ? ", NEON conversion" : "",
		frames * 1e6 / elapsed, convertTime / 1e3 / frames,
		cpu / 1e3 / frames, bytes / 1024.0 / frames);
	return 0;
}
