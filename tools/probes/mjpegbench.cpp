/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * mjpegbench: how long a webcam's MJPEG frame takes to become B_RGB32
 * (BGRA in memory), with libjpeg-turbo or with FFmpeg's decoder and
 * swscale, on 1 to 4 threads each decoding frames of its own.
 * Usage: mjpegbench <file.jpg> <frames per thread> [<threads>]
 */

#include <OS.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <turbojpeg.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}


static uint8* sJpeg;
static size_t sJpegSize;
static int sWidth, sHeight;
static int sFrames;


enum Mode { TURBO, TURBO_FAST, FFMPEG };
static const char* kModeNames[] = { "libjpeg-turbo", "libjpeg-turbo fast",
	"ffmpeg+swscale" };


struct Job {
	Mode mode;
	bigtime_t cpu;
	int failed;
};


static status_t
run(void* data)
{
	Job* job = (Job*)data;
	uint8* rgb = (uint8*)malloc((size_t)sWidth * sHeight * 4);
	thread_info before, after;
	get_thread_info(find_thread(NULL), &before);

	if (job->mode != FFMPEG) {
		tjhandle handle = tj3Init(TJINIT_DECOMPRESS);
		if (job->mode == TURBO_FAST) {
			tj3Set(handle, TJPARAM_FASTUPSAMPLE, 1);
			tj3Set(handle, TJPARAM_FASTDCT, 1);
		}
		for (int i = 0; i < sFrames; i++) {
			if (tj3DecompressHeader(handle, sJpeg, sJpegSize) != 0
				|| tj3Decompress8(handle, sJpeg, sJpegSize, rgb,
					sWidth * 4, TJPF_BGRA) != 0) {
				job->failed++;
			}
		}
		tj3Destroy(handle);
	} else {
		const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
		AVCodecContext* context = avcodec_alloc_context3(codec);
		context->thread_count = 1;
		avcodec_open2(context, codec, NULL);
		AVPacket* packet = av_packet_alloc();
		AVFrame* frame = av_frame_alloc();
		SwsContext* scaler = NULL;
		for (int i = 0; i < sFrames; i++) {
			packet->data = sJpeg;
			packet->size = sJpegSize;
			if (avcodec_send_packet(context, packet) != 0
				|| avcodec_receive_frame(context, frame) != 0) {
				job->failed++;
				continue;
			}
			scaler = sws_getCachedContext(scaler, frame->width,
				frame->height, (AVPixelFormat)frame->format, sWidth, sHeight,
				AV_PIX_FMT_BGRA, SWS_POINT, NULL, NULL, NULL);
			uint8* planes[1] = { rgb };
			int strides[1] = { sWidth * 4 };
			sws_scale(scaler, frame->data, frame->linesize, 0, frame->height,
				planes, strides);
		}
		sws_freeContext(scaler);
		av_frame_free(&frame);
		av_packet_free(&packet);
		avcodec_free_context(&context);
	}

	get_thread_info(find_thread(NULL), &after);
	job->cpu = after.user_time + after.kernel_time - before.user_time
		- before.kernel_time;
	free(rgb);
	return B_OK;
}


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: mjpegbench <file.jpg> <frames per thread> "
			"[<threads>]\n");
		return 1;
	}
	FILE* file = fopen(argv[1], "rb");
	if (file == NULL) {
		perror(argv[1]);
		return 1;
	}
	fseek(file, 0, SEEK_END);
	sJpegSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	sJpeg = (uint8*)malloc(sJpegSize + AV_INPUT_BUFFER_PADDING_SIZE);
	memset(sJpeg + sJpegSize, 0, AV_INPUT_BUFFER_PADDING_SIZE);
	fread(sJpeg, 1, sJpegSize, file);
	fclose(file);
	sFrames = atoi(argv[2]);
	int maxThreads = argc > 3 ? atoi(argv[3]) : 4;

	tjhandle handle = tj3Init(TJINIT_DECOMPRESS);
	if (tj3DecompressHeader(handle, sJpeg, sJpegSize) != 0) {
		fprintf(stderr, "not a JPEG: %s\n", tj3GetErrorStr(handle));
		return 1;
	}
	sWidth = tj3Get(handle, TJPARAM_JPEGWIDTH);
	sHeight = tj3Get(handle, TJPARAM_JPEGHEIGHT);
	int subsampling = tj3Get(handle, TJPARAM_SUBSAMP);
	tj3Destroy(handle);
	printf("%s: %dx%d, %zu bytes, subsampling %d\n", argv[1], sWidth,
		sHeight, sJpegSize, subsampling);

	for (int mode = TURBO; mode <= FFMPEG; mode++) {
		for (int threads = 1; threads <= maxThreads; threads *= 2) {
			Job jobs[4];
			thread_id ids[4];
			bigtime_t start = system_time();
			for (int t = 0; t < threads; t++) {
				jobs[t].mode = (Mode)mode;
				jobs[t].cpu = 0;
				jobs[t].failed = 0;
				ids[t] = spawn_thread(run, "decode", B_NORMAL_PRIORITY,
					&jobs[t]);
				resume_thread(ids[t]);
			}
			bigtime_t cpu = 0;
			int failed = 0;
			for (int t = 0; t < threads; t++) {
				status_t result;
				wait_for_thread(ids[t], &result);
				cpu += jobs[t].cpu;
				failed += jobs[t].failed;
			}
			bigtime_t elapsed = system_time() - start;
			int frames = sFrames * threads;
			printf("%-19s %d thread(s): %6.1f frames/s, %5.2f ms CPU a frame"
				"%s\n", kModeNames[mode], threads,
				frames * 1000000.0 / elapsed, cpu / 1000.0 / frames,
				failed ? " (failures)" : "");
		}
	}
	return 0;
}
