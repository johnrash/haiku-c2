/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * keylog <signature> <log file>: an application with that signature whose
 * window logs every key it gets (the key code, the bytes, the modifiers),
 * one line each, for the Remote Control's per-application profiles: two
 * of them with different signatures are two applications.
 */
#include <stdio.h>

#include <Application.h>
#include <View.h>
#include <Window.h>


static FILE* sLog;


class LogView : public BView {
public:
	LogView(BRect frame)
		:
		BView(frame, "log", B_FOLLOW_ALL, B_WILL_DRAW)
	{
	}

	void KeyDown(const char* bytes, int32 numBytes)
	{
		_Log(Window()->CurrentMessage());
	}

	void MessageReceived(BMessage* message)
	{
		if (message->what == B_UNMAPPED_KEY_DOWN)
			_Log(message);
		else
			BView::MessageReceived(message);
	}

private:
	void _Log(BMessage* message)
	{
		if (message == NULL)
			return;
		fprintf(sLog, "key 0x%02x bytes \"%s\" modifiers 0x%x%s\n",
			(unsigned)message->GetInt32("key", 0),
			message->GetString("bytes", ""),
			(unsigned)message->GetInt32("modifiers", 0),
			message->what == B_UNMAPPED_KEY_DOWN ? " (unmapped)" : "");
		fflush(sLog);
	}
};


int
main(int argc, char** argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: keylog <signature> <log file>\n");
		return 1;
	}
	sLog = fopen(argv[2], "a");
	if (sLog == NULL) {
		perror(argv[2]);
		return 1;
	}
	BApplication app(argv[1]);
	BWindow* window = new BWindow(BRect(100, 100, 400, 200), argv[1],
		B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE);
	LogView* view = new LogView(window->Bounds());
	window->AddChild(view);
	view->MakeFocus();
	window->Show();
	app.Run();
	return 0;
}
