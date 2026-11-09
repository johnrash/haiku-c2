/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * Runs a screen saver add-on in a window the way the screen saver runner
 * does (StartSaver(), then Draw() with the window locked whenever a tick has
 * passed) and prints, every 5 seconds, how often Draw() ran, how long it took
 * on average and at most, and how many calls took longer than a 60 Hz frame.
 *
 *   saverrun <add-on> [-direct] [<seconds> [<width> <height>]]
 *       [<setting>=<int32>...]
 *
 * Without a size the window covers the screen. The settings go to the saver
 * as its saved state. The runner's window is a BDirectWindow; this one is
 * one only with -direct (DirectConnected() and DirectDraw() then reach the
 * saver as from the runner): without, GL savers draw through a bitmap.
 *
 * Build: $CXX -O2 $HAIKU_CPPFLAGS -I$HAIKU_SRC/headers/os/add-ons/screen_saver \
 *            -o saverrun saverrun.cpp -lbe -lgame -lscreensaver
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <DirectWindow.h>
#include <Screen.h>
#include <String.h>
#include <ScreenSaver.h>
#include <View.h>
#include <Window.h>
#include <image.h>


typedef BScreenSaver* (*instantiate_func)(BMessage*, image_id);


class DirectWindow : public BDirectWindow {
public:
	DirectWindow(BRect frame, BScreenSaver* saver)
		:
		BDirectWindow(frame, "saverrun", B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL, B_NOT_MOVABLE | B_NOT_RESIZABLE),
		fSaver(saver)
	{
	}

	~DirectWindow()
	{
		// no DirectConnected() once it is gone
		Hide();
		Sync();
	}

	virtual void DirectConnected(direct_buffer_info* info)
	{
		fSaver->DirectConnected(info);
	}

private:
	BScreenSaver*	fSaver;
};


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <add-on> [-direct] [<seconds> [<width> "
			"<height>]] [<setting>=<int32>...]\n", argv[0]);
		return 1;
	}

	// settings for the saver; the rest are positional
	BMessage settings;
	const char* positional[4];
	int positionalCount = 0;
	bool direct = false;
	for (int i = 2; i < argc; i++) {
		const char* equals = strchr(argv[i], '=');
		if (strcmp(argv[i], "-direct") == 0)
			direct = true;
		else if (equals != NULL) {
			BString name(argv[i], equals - argv[i]);
			settings.AddInt32(name.String(), atoi(equals + 1));
		} else if (positionalCount < 3)
			positional[positionalCount++] = argv[i];
	}

	BApplication app("application/x-vnd.test-saverrun");

	image_id image = load_add_on(argv[1]);
	if (image < 0) {
		fprintf(stderr, "saverrun: %s: %s\n", argv[1], strerror(image));
		return 1;
	}
	instantiate_func instantiate;
	if (get_image_symbol(image, "instantiate_screen_saver", B_SYMBOL_TYPE_TEXT,
			(void**)&instantiate) != B_OK) {
		fprintf(stderr, "saverrun: no instantiate_screen_saver\n");
		return 1;
	}

	BScreenSaver* saver = instantiate(&settings, image);

	bigtime_t duration = (bigtime_t)((positionalCount > 0
		? atof(positional[0]) : 10) * 1000000);
	BRect frame = BScreen().Frame();
	if (positionalCount > 2)
		frame.Set(0, 0, atoi(positional[1]) - 1, atoi(positional[2]) - 1);

	BWindow* window;
	if (direct)
		window = new DirectWindow(frame, saver);
	else {
		window = new BWindow(frame, "saverrun", B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL, B_NOT_MOVABLE | B_NOT_RESIZABLE);
	}
	BView* view = new BView(window->Bounds(), "saver", B_FOLLOW_ALL,
		B_WILL_DRAW);
	view->SetViewColor(0, 0, 0);
	window->AddChild(view);
	window->Show();

	window->Lock();
	status_t status = saver->StartSaver(view, false);
	window->Unlock();
	if (status != B_OK) {
		fprintf(stderr, "saverrun: StartSaver: %s\n", strerror(status));
		return 1;
	}

	// the runner's loop: sleep for the smallest tick seen, call Draw() once a
	// tick has passed since the last call
	bigtime_t tickBase = 50000;
	bigtime_t lastTick = 0;
	bigtime_t start = system_time();
	bigtime_t report = start;
	bigtime_t drawing = 0;
	bigtime_t longest = 0;
	int32 frameNumber = 0;
	int32 draws = 0;
	int32 late = 0;
	while (system_time() - start < duration) {
		bigtime_t tick = saver->TickSize();
		if (tick < tickBase)
			tickBase = tick > 0 ? tick : 0;
		snooze(tickBase);
		if (system_time() - lastTick < tick)
			continue;
		lastTick = system_time();

		window->Lock();
		bigtime_t before = system_time();
		if (direct)
			saver->DirectDraw(frameNumber);
		saver->Draw(view, frameNumber++);
		view->Sync();
		bigtime_t took = system_time() - before;
		window->Unlock();
		drawing += took;
		if (took > longest)
			longest = took;
		if (took > 1000000 / 60)
			late++;
		draws++;

		bigtime_t now = system_time();
		if (now - report >= 5000000) {
			printf("%.1f draws/s, %.1f ms each, at most %.1f ms, %" B_PRId32
				" over 16.7 ms\n", draws * 1000000.0 / (now - report),
				drawing / 1000.0 / draws, longest / 1000.0, late);
			fflush(stdout);
			report = now;
			draws = 0;
			drawing = 0;
			longest = 0;
			late = 0;
		}
	}

	window->Lock();
	saver->StopSaver();
	window->Quit();
	delete saver;
	unload_add_on(image);
	return 0;
}
