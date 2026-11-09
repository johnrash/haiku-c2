/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * beepflash <device> [<seconds>]: once a second, a 100 ms 1 kHz beep
 * written straight into a multi_audio device's playback ring, and a full
 * screen white flash at the time the driver says that beep starts playing
 * (the media_server must not have the device open). Recorded through the
 * HDMI capture (tools/video/avsync.py), it shows the delay between what
 * the driver's time stamps claim and what comes out of the HDMI port,
 * without the media kit in between.
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Application.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>

#include <hmulti_audio.h>


static const int kMaxBuffers = 8;
static const int kMaxChannels = 8;

static BWindow* sWindow;
static BView* sView;
static bigtime_t sFlashAt;
static sem_id sFlashSem;


static void
set_color(uint8 level)
{
	if (!sWindow->Lock())
		return;
	sView->SetViewColor(level, level, level);
	sView->Invalidate();
	sWindow->Sync();
	sWindow->Unlock();
}


static status_t
flasher(void*)
{
	while (acquire_sem(sFlashSem) == B_OK) {
		snooze_until(sFlashAt, B_SYSTEM_TIMEBASE);
		set_color(255);
		snooze(100000);
		set_color(0);
	}
	return B_OK;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		printf("usage: beepflash <device> [<seconds>]\n");
		return 1;
	}
	int seconds = argc > 2 ? atoi(argv[2]) : 20;

	BApplication app("application/x-vnd.c2-beepflash");
	BRect frame = BScreen().Frame();
	sWindow = new BWindow(frame, "beepflash", B_NO_BORDER_WINDOW_LOOK,
		B_FLOATING_ALL_WINDOW_FEEL, B_NOT_MOVABLE | B_NOT_RESIZABLE);
	sView = new BView(sWindow->Bounds(), "flash", B_FOLLOW_ALL, B_WILL_DRAW);
	sView->SetViewColor(0, 0, 0);
	sWindow->AddChild(sView);
	sWindow->Show();
	snooze(500000);

	int fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		printf("beepflash: %s: %s\n", argv[1], strerror(errno));
		return 1;
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
		printf("beepflash: B_MULTI_GET_BUFFERS: %s\n", strerror(errno));
		return 1;
	}
	int buffers = list.return_playback_buffers;
	int channels = list.return_playback_channels;
	uint32 frames = list.return_playback_buffer_size;
	double rate = 48000;
	bigtime_t bufferTime = (bigtime_t)(frames * 1000000.0 / rate);
	printf("beepflash: %d buffers of %u frames (%" B_PRIdBIGTIME " us)\n",
		buffers, (unsigned)frames, bufferTime);

	sFlashSem = create_sem(0, "flash");
	resume_thread(spawn_thread(flasher, "flasher", B_DISPLAY_PRIORITY, NULL));

	multi_buffer_info info;
	memset(&info, 0, sizeof(info));
	info.info_size = sizeof(info);
	int beepBuffers = (int)(100000 / bufferTime) + 1;
	int perSecond = (int)(1000000 / bufferTime);
	int64 exchange = 0;
	uint64 beepFrame = 0;
	bigtime_t start = system_time();
	while (system_time() - start < seconds * 1000000LL) {
		if (ioctl(fd, B_MULTI_BUFFER_EXCHANGE, &info, sizeof(info)) != 0) {
			printf("beepflash: B_MULTI_BUFFER_EXCHANGE: %s\n",
				strerror(errno));
			break;
		}
		exchange++;
		// the driver reads playback_buffer_cycle since played_real_time;
		// the next one is free and starts a buffer time later
		int next = (info.playback_buffer_cycle + 1) % buffers;
		int phase = exchange % perSecond;
		bool beep = exchange > 2 * perSecond && phase < beepBuffers;
		if (beep && phase == 0) {
			sFlashAt = info.played_real_time + bufferTime;
			release_sem(sFlashSem);
			beepFrame = 0;
		}
		for (int c = 0; c < channels && c < kMaxChannels; c++) {
			for (uint32 f = 0; f < frames; f++) {
				int16* sample = (int16*)(playback[next][c].base
					+ f * playback[next][c].stride);
				*sample = beep ? (int16)(12000
					* sin(2 * M_PI * 1000 * (beepFrame + f) / rate)) : 0;
			}
		}
		if (beep)
			beepFrame += frames;
	}
	close(fd);
	sWindow->Lock();
	sWindow->Quit();
	return 0;
}
