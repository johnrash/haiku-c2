/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * camview: connects the default video input (a webcam's node) to a consumer
 * of its own at a frame size of choice, B_RGB32, and counts what arrives:
 * frames a second, buffers late or bad, and the CPU the producer's team
 * used; optionally shows the video in a window.
 * Usage: camview <width> <height> <seconds> [window] [repeat <n>]
 *   window: show the frames (scaled to at most 960x540)
 *   repeat <n>: connect, run and tear down n times
 * Exit status 0 if frames arrived every run.
 */

#include <Application.h>
#include <Bitmap.h>
#include <Buffer.h>
#include <BufferConsumer.h>
#include <MediaEventLooper.h>
#include <MediaRoster.h>
#include <TimeSource.h>
#include <View.h>
#include <Window.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static team_id
team_of_node(const media_node& node)
{
	port_info info;
	if (get_port_info(node.port, &info) != B_OK)
		return -1;
	return info.team;
}


static bigtime_t
team_cpu(team_id team)
{
	team_usage_info usage;
	if (team < 0 || get_team_usage_info(team, B_TEAM_USAGE_SELF, &usage)
			!= B_OK) {
		return 0;
	}
	return usage.user_time + usage.kernel_time;
}


class VideoView : public BView {
public:
	VideoView(BRect frame)
		:
		BView(frame, "video", B_FOLLOW_ALL, B_WILL_DRAW),
		fBitmap(NULL)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	void SetBitmap(BBitmap* bitmap)
	{
		fBitmap = bitmap;
	}

	virtual void Draw(BRect update)
	{
		if (fBitmap != NULL)
			DrawBitmap(fBitmap, fBitmap->Bounds(), Bounds());
	}

private:
	BBitmap*	fBitmap;
};


class Consumer : public BMediaEventLooper, public BBufferConsumer {
public:
	Consumer(uint32 width, uint32 height, BWindow* window, VideoView* view)
		:
		BMediaNode("camview consumer"),
		BMediaEventLooper(),
		BBufferConsumer(B_MEDIA_RAW_VIDEO),
		fWidth(width),
		fHeight(height),
		fWindow(window),
		fView(view),
		fBitmap(NULL),
		fFrames(0),
		fBad(0),
		fFirst(0),
		fLast(0)
	{
		if (view != NULL) {
			fBitmap = new BBitmap(BRect(0, 0, width - 1, height - 1),
				B_RGB32);
			view->SetBitmap(fBitmap);
		}
	}

	~Consumer()
	{
		BMediaEventLooper::Quit();
		delete fBitmap;
	}

	virtual BMediaAddOn* AddOn(int32* internalID) const
	{
		return NULL;
	}

	virtual void NodeRegistered()
	{
		fInput.destination.port = ControlPort();
		fInput.destination.id = 0;
		fInput.node = Node();
		fInput.source = media_source::null;
		fInput.format.type = B_MEDIA_RAW_VIDEO;
		fInput.format.u.raw_video = media_raw_video_format::wildcard;
		strcpy(fInput.name, "camview input");
		Run();
	}

	virtual status_t HandleMessage(int32 message, const void* data,
		size_t size)
	{
		if (BBufferConsumer::HandleMessage(message, data, size) == B_OK)
			return B_OK;
		return BMediaEventLooper::HandleMessage(message, data, size);
	}

	virtual void HandleEvent(const media_timed_event* event,
		bigtime_t lateness, bool realTimeEvent)
	{
	}

	virtual status_t AcceptFormat(const media_destination& destination,
		media_format* format)
	{
		if (destination != fInput.destination)
			return B_MEDIA_BAD_DESTINATION;
		if (format->type == B_MEDIA_NO_TYPE)
			format->type = B_MEDIA_RAW_VIDEO;
		if (format->type != B_MEDIA_RAW_VIDEO)
			return B_MEDIA_BAD_FORMAT;
		media_video_display_info& display = format->u.raw_video.display;
		if (display.format != B_RGB32
			&& display.format
				!= media_raw_video_format::wildcard.display.format) {
			return B_MEDIA_BAD_FORMAT;
		}
		display.format = B_RGB32;
		display.line_width = fWidth;
		display.line_count = fHeight;
		display.bytes_per_row = fWidth * 4;
		return B_OK;
	}

	virtual status_t GetNextInput(int32* cookie, media_input* input)
	{
		if (*cookie != 0)
			return B_BAD_INDEX;
		*input = fInput;
		(*cookie)++;
		return B_OK;
	}

	virtual void DisposeInputCookie(int32 cookie)
	{
	}

	virtual void BufferReceived(BBuffer* buffer)
	{
		const media_header* header = buffer->Header();
		size_t size = (size_t)fWidth * fHeight * 4;
		if (header->size_used < size)
			fBad++;
		else {
			bigtime_t now = system_time();
			if (fFrames++ == 0)
				fFirst = now;
			fLast = now;
			if (fBitmap != NULL && fWindow->LockWithTimeout(10000) == B_OK) {
				memcpy(fBitmap->Bits(), buffer->Data(), size);
				fView->Invalidate();
				fWindow->Unlock();
			}
		}
		buffer->Recycle();
	}

	virtual void ProducerDataStatus(const media_destination& destination,
		int32 status, bigtime_t when)
	{
	}

	virtual status_t GetLatencyFor(const media_destination& destination,
		bigtime_t* latency, media_node_id* timeSource)
	{
		*latency = 20000;
		*timeSource = TimeSource()->ID();
		return B_OK;
	}

	virtual status_t Connected(const media_source& producer,
		const media_destination& where, const media_format& format,
		media_input* input)
	{
		fInput.source = producer;
		fInput.format = format;
		*input = fInput;
		return B_OK;
	}

	virtual void Disconnected(const media_source& producer,
		const media_destination& where)
	{
		fInput.source = media_source::null;
	}

	virtual status_t FormatChanged(const media_source& producer,
		const media_destination& consumer, int32 changeTag,
		const media_format& format)
	{
		return B_OK;
	}

	void Reset()
	{
		fFrames = 0;
		fBad = 0;
		fFirst = fLast = 0;
	}

	int32 Frames() const { return fFrames; }
	int32 Bad() const { return fBad; }
	double Rate() const
	{
		return fFrames > 1
			? (fFrames - 1) * 1000000.0 / (fLast - fFirst) : 0;
	}

private:
	uint32			fWidth;
	uint32			fHeight;
	BWindow*		fWindow;
	VideoView*		fView;
	BBitmap*		fBitmap;
	media_input		fInput;
	volatile int32	fFrames;
	volatile int32	fBad;
	bigtime_t		fFirst;
	bigtime_t		fLast;
};


static bool
run_once(BMediaRoster* roster, Consumer* consumer, uint32 width,
	uint32 height, double seconds)
{
	media_node producer;
	status_t status = roster->GetVideoInput(&producer);
	if (status != B_OK) {
		printf("no video input: %s\n", strerror(status));
		return false;
	}

	media_output output;
	media_input input;
	int32 count = 0;
	status = roster->GetFreeOutputsFor(producer, &output, 1, &count,
		B_MEDIA_RAW_VIDEO);
	if (status != B_OK || count < 1) {
		printf("no free output on the video input (%s)\n", strerror(status));
		roster->ReleaseNode(producer);
		return false;
	}
	count = 0;
	status = roster->GetFreeInputsFor(consumer->Node(), &input, 1, &count,
		B_MEDIA_RAW_VIDEO);
	if (status != B_OK || count < 1) {
		printf("no free input on the consumer\n");
		roster->ReleaseNode(producer);
		return false;
	}

	media_format format;
	format.type = B_MEDIA_RAW_VIDEO;
	format.u.raw_video = media_raw_video_format::wildcard;
	format.u.raw_video.display.format = B_RGB32;
	format.u.raw_video.display.line_width = width;
	format.u.raw_video.display.line_count = height;
	format.u.raw_video.display.bytes_per_row = width * 4;
	status = roster->Connect(output.source, input.destination, &format,
		&output, &input);
	if (status != B_OK) {
		printf("connect %ux%u: %s\n", (unsigned)width, (unsigned)height,
			strerror(status));
		roster->ReleaseNode(producer);
		return false;
	}

	media_node timeSource;
	roster->GetTimeSource(&timeSource);
	roster->SetTimeSourceFor(producer.node, timeSource.node);
	roster->SetTimeSourceFor(consumer->ID(), timeSource.node);
	roster->ReleaseNode(timeSource);

	bigtime_t latency = 0;
	roster->GetLatencyFor(producer, &latency);
	roster->SetProducerRunModeDelay(producer, latency);

	team_id producerTeam = team_of_node(producer);
	BTimeSource* source = roster->MakeTimeSourceFor(producer);
	bigtime_t start = source->PerformanceTimeFor(BTimeSource::RealTime()
		+ latency + 50000);
	source->Release();
	consumer->Reset();
	roster->StartNode(producer, start);
	roster->StartNode(consumer->Node(), start);

	// the first second is the camera starting
	snooze(1000000);
	consumer->Reset();
	bigtime_t cpuBefore = team_cpu(producerTeam);
	bigtime_t before = system_time();
	snooze((bigtime_t)(seconds * 1000000));
	bigtime_t elapsed = system_time() - before;
	bigtime_t cpu = team_cpu(producerTeam) - cpuBefore;
	int32 frames = consumer->Frames();
	printf("%ux%u: %d frames, %.1f a second, %d bad; producer team %.0f%% "
		"of one core\n", (unsigned)width, (unsigned)height, (int)frames,
		consumer->Rate(), (int)consumer->Bad(), cpu * 100.0 / elapsed);
	fflush(stdout);

	roster->StopNode(consumer->Node(), 0, true);
	roster->StopNode(producer, 0, true);
	roster->Disconnect(output.node.node, output.source, input.node.node,
		input.destination);
	roster->ReleaseNode(producer);
	return frames > 0;
}


int
main(int argc, char** argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: camview <width> <height> <seconds> [window] "
			"[repeat <n>]\n");
		return 1;
	}
	uint32 width = atoi(argv[1]);
	uint32 height = atoi(argv[2]);
	double seconds = atof(argv[3]);
	bool window = false;
	int repeat = 1;
	for (int i = 4; i < argc; i++) {
		if (strcmp(argv[i], "window") == 0)
			window = true;
		else if (strcmp(argv[i], "repeat") == 0 && i + 1 < argc)
			repeat = atoi(argv[++i]);
	}

	BApplication app("application/x-vnd.c2-camview");

	BWindow* videoWindow = NULL;
	VideoView* view = NULL;
	if (window) {
		float scale = 1;
		if (width > 960)
			scale = 960.0 / width;
		BRect frame(0, 0, width * scale - 1, height * scale - 1);
		frame.OffsetTo(40, 60);
		videoWindow = new BWindow(frame, "camview", B_TITLED_WINDOW,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE);
		view = new VideoView(videoWindow->Bounds());
		videoWindow->AddChild(view);
		videoWindow->Show();
	}

	status_t status;
	BMediaRoster* roster = BMediaRoster::Roster(&status);
	if (roster == NULL) {
		printf("no media roster: %s\n", strerror(status));
		return 1;
	}

	Consumer* consumer = new Consumer(width, height, videoWindow, view);
	status = roster->RegisterNode(consumer);
	if (status != B_OK) {
		printf("register consumer: %s\n", strerror(status));
		return 1;
	}

	bool good = true;
	for (int i = 0; i < repeat; i++)
		good &= run_once(roster, consumer, width, height, seconds);

	roster->UnregisterNode(consumer);
	consumer->Release();
	if (videoWindow != NULL) {
		videoWindow->Lock();
		videoWindow->Quit();
	}
	return good ? 0 : 1;
}
