/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * loopercrash [debugger|segv]: a crash in a BLooper's MessageReceived(), with
 * libbe and libroot frames (and their unwind tables) below it and a second
 * thread waiting elsewhere; for checking the Debugger's crash reports.
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <Looper.h>
#include <OS.h>


static const char* sKind = "debugger";


class CrashLooper : public BLooper {
public:
	CrashLooper()
		:
		BLooper("crash looper")
	{
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != 'cras') {
			BLooper::MessageReceived(message);
			return;
		}
		if (strcmp(sKind, "segv") == 0)
			*(volatile int*)16 = 1;
		debugger("crashtest: debugger() in MessageReceived");
	}
};


static status_t
waiter(void*)
{
	snooze(1000000000LL);
	return B_OK;
}


int
main(int argc, char** argv)
{
	if (argc > 1)
		sKind = argv[1];
	resume_thread(spawn_thread(waiter, "waiter", B_NORMAL_PRIORITY, NULL));
	CrashLooper* looper = new CrashLooper;
	looper->Run();
	looper->PostMessage('cras');
	snooze(1000000000LL);
	return 0;
}
