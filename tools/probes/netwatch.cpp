/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * netwatch [<seconds>] [<repeat>]: watches the network stack's interface
 * and link notifications (start_watching_network()) for <seconds>
 * (default 10) and prints each one with its opcode; with <repeat>, starts
 * and stops watching that many times first (the generic syscall's add and
 * remove listener paths). Exits 1 if nothing arrived.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <NetworkNotifications.h>


class WatchApp : public BApplication {
public:
	WatchApp()
		:
		BApplication("application/x-vnd.c2-netwatch"),
		fCount(0)
	{
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != B_NETWORK_MONITOR) {
			BApplication::MessageReceived(message);
			return;
		}
		int32 opcode = message->GetInt32("opcode", -1);
		const char* interface = message->GetString("interface", "-");
		printf("netwatch: notification, opcode %d, interface %s\n",
			(int)opcode, interface);
		fflush(stdout);
		fCount++;
	}

	int32 Count() const { return fCount; }

private:
	int32 fCount;
};


static status_t
quit_later(void* data)
{
	snooze((bigtime_t)(addr_t)data * 1000000);
	be_app->PostMessage(B_QUIT_REQUESTED);
	return B_OK;
}


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 10;
	int repeat = argc > 2 ? atoi(argv[2]) : 0;
	WatchApp app;

	for (int i = 0; i < repeat; i++) {
		if (start_watching_network(B_WATCH_NETWORK_INTERFACE_CHANGES,
				be_app_messenger) != B_OK
			|| stop_watching_network(be_app_messenger) != B_OK) {
			printf("netwatch: start/stop %d failed\n", i);
			return 1;
		}
	}
	status_t status = start_watching_network(
		B_WATCH_NETWORK_INTERFACE_CHANGES | B_WATCH_NETWORK_LINK_CHANGES,
		be_app_messenger);
	if (status != B_OK) {
		printf("netwatch: start_watching_network: %s\n", strerror(status));
		return 1;
	}
	printf("netwatch: watching for %d s (%d start/stop rounds before)\n",
		seconds, repeat);
	fflush(stdout);

	resume_thread(spawn_thread(quit_later, "quit later", B_NORMAL_PRIORITY,
		(void*)(addr_t)seconds));
	app.Run();
	stop_watching_network(be_app_messenger);
	printf("netwatch: %d notification(s)\n", (int)app.Count());
	return app.Count() > 0 ? 0 : 1;
}
