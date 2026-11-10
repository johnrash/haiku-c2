/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 *   inputwatch [seconds] [device]
 *
 * Prints the input events app_server delivers, through a small off-screen
 * window that asks for all pointer and keyboard events. With a device name,
 * that device is stopped first and started again at the end (milestone M79).
 *
 * Build: $CXX -O2 $HAIKU_CXX_INCLUDES -o inputwatch inputwatch.cpp -lbe
 */


#include <stdio.h>
#include <stdlib.h>

#include <Application.h>
#include <Input.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <MessageRunner.h>
#include <View.h>
#include <Window.h>


static const uint32 kMsgDone = 'done';
static bigtime_t sStart;


class WatchView : public BView {
public:
	WatchView()
		:
		BView(BRect(0, 0, 1, 1), "watch", B_FOLLOW_NONE, 0),
		fCount(0)
	{
	}

	virtual void AttachedToWindow()
	{
		SetEventMask(B_POINTER_EVENTS | B_KEYBOARD_EVENTS,
			B_NO_POINTER_HISTORY);
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case B_MOUSE_MOVED:
			case B_MOUSE_DOWN:
			case B_MOUSE_UP:
			case B_MOUSE_WHEEL_CHANGED:
			case B_KEY_DOWN:
			case B_KEY_UP:
			case B_UNMAPPED_KEY_DOWN:
			case B_UNMAPPED_KEY_UP:
			case B_MODIFIERS_CHANGED:
				_Report(message);
				break;
			default:
				BView::MessageReceived(message);
				break;
		}
	}

	virtual void MouseMoved(BPoint, uint32, const BMessage*) {}
	virtual void MouseDown(BPoint) {}
	virtual void MouseUp(BPoint) {}
	virtual void KeyDown(const char*, int32) {}
	virtual void KeyUp(const char*, int32) {}

	void Summary()
	{
		printf("%d events\n", fCount);
	}

private:
	void _Report(BMessage* message)
	{
		fCount++;
		if (fCount > 40)
			return;
		int32 key = -1;
		int32 buttons = 0;
		int32 modifiers = 0;
		BPoint where(-1, -1);
		message->FindInt32("key", &key);
		message->FindInt32("buttons", &buttons);
		message->FindInt32("modifiers", &modifiers);
		message->FindPoint("be:view_where", &where);
		printf("%6.2f s '%c%c%c%c' key %" B_PRId32 " buttons %" B_PRId32
			" modifiers 0x%" B_PRIx32 " at %.0f,%.0f\n",
			(system_time() - sStart) / 1e6,
			char(message->what >> 24), char(message->what >> 16),
			char(message->what >> 8), char(message->what), key, buttons,
			modifiers, where.x, where.y);
		fflush(stdout);
	}

	int fCount;
};


class WatchWindow : public BWindow {
public:
	WatchWindow()
		:
		BWindow(BRect(-40, -40, -30, -30), "inputwatch", B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL, B_AVOID_FOCUS | B_NOT_MOVABLE),
		fView(new WatchView)
	{
		AddChild(fView);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kMsgDone) {
			fView->Summary();
			be_app->PostMessage(B_QUIT_REQUESTED);
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	WatchView* fView;
};


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 15;
	BApplication app("application/x-vnd.c2-inputwatch");

	BInputDevice* device = NULL;
	if (argc > 2) {
		device = find_input_device(argv[2]);
		if (device == NULL) {
			printf("no device \"%s\"\n", argv[2]);
			return 1;
		}
		device->Stop();
		printf("%s stopped\n", argv[2]);
	}

	sStart = system_time();
	WatchWindow* window = new WatchWindow;
	window->Show();
	BMessage done(kMsgDone);
	BMessageRunner runner(BMessenger(window), &done, seconds * 1000000LL, 1);
	app.Run();

	if (device != NULL) {
		device->Start();
		printf("%s started again\n", argv[2]);
	}
	return 0;
}
