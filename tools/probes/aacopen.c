/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * aacopen [rate] [channels] [bit rate]: opens FFmpeg's AAC encoder with the
 * settings the FFmpeg plugin gives it (planar floats, the time base in
 * samples, experimental compliance), with FFmpeg's log on, and sets up the
 * 16-bit to planar float conversion; prints what failed, the frame size and
 * the extradata.
 * Build: $CC -O2 $HAIKU_CPPFLAGS -I<ffmpeg6>/develop/headers
 * -L<ffmpeg6>/develop/lib -o aacopen aacopen.c -lavcodec -lswresample
 * -lavutil
 */
#include <stdio.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>


int
main(int argc, char** argv)
{
	int rate = argc > 1 ? atoi(argv[1]) : 16000;
	int channels = argc > 2 ? atoi(argv[2]) : 1;
	int bitRate = argc > 3 ? atoi(argv[3]) : 32000;
	av_log_set_level(AV_LOG_DEBUG);

	const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (codec == NULL) {
		printf("no AAC encoder\n");
		return 1;
	}
	printf("encoder %s, sample formats:", codec->name);
	for (const enum AVSampleFormat* f = codec->sample_fmts;
			f != NULL && *f != AV_SAMPLE_FMT_NONE; f++)
		printf(" %s", av_get_sample_fmt_name(*f));
	printf("\n");

	AVCodecContext* context = avcodec_alloc_context3(codec);
	context->sample_rate = rate;
	av_channel_layout_default(&context->ch_layout, channels);
	context->sample_fmt = codec->sample_fmts[0];
	context->time_base = (AVRational){1, rate};
	context->bit_rate = bitRate;
	context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

	int result = avcodec_open2(context, codec, NULL);
	printf("avcodec_open2: %d (%s)\n", result, av_err2str(result));
	if (result < 0)
		return 1;
	printf("frame size %d, extradata %d bytes:", context->frame_size,
		context->extradata_size);
	for (int i = 0; i < context->extradata_size; i++)
		printf(" %02x", context->extradata[i]);
	printf("\n");

	SwrContext* resample = NULL;
	result = swr_alloc_set_opts2(&resample, &context->ch_layout,
		context->sample_fmt, rate, &context->ch_layout, AV_SAMPLE_FMT_S16,
		rate, 0, NULL);
	printf("swr_alloc_set_opts2: %d (%s)\n", result, av_err2str(result));
	result = swr_init(resample);
	printf("swr_init: %d (%s)\n", result, av_err2str(result));

	swr_free(&resample);
	avcodec_free_context(&context);
	return 0;
}
