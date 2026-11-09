/* Loads an image through the Translation Kit into a BBitmap (the
   BBitmapStream path ShowImage uses) and prints its size. */
#include <Application.h>
#include <Bitmap.h>
#include <OS.h>
#include <TranslationUtils.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


static void
segv(int, siginfo_t* info, void*)
{
	fprintf(stderr, "getbitmap: segment violation at %p\n", info->si_addr);
	// the areas around it: is the app_server memory clone big enough?
	ssize_t cookie = 0;
	area_info area;
	addr_t fault = (addr_t)info->si_addr;
	while (get_next_area_info(0, &cookie, &area) == B_OK) {
		addr_t start = (addr_t)area.address;
		if (start + area.size + 0x10000000 > fault && start < fault + 0x10000000) {
			fprintf(stderr, "  area %d \"%s\": %p - %p (%zu bytes)\n",
				(int)area.area, area.name, area.address,
				(void*)(start + area.size), area.size);
		}
	}
	// and app_server's side of this team's memory ("heap:<team>:...")
	char prefix[32];
	snprintf(prefix, sizeof(prefix), "heap:%d:", (int)getpid());
	int32 teamCookie = 0;
	team_info team;
	while (get_next_team_info(&teamCookie, &team) == B_OK) {
		if (strstr(team.args, "app_server") == NULL)
			continue;
		cookie = 0;
		while (get_next_area_info(team.team, &cookie, &area) == B_OK) {
			if (strncmp(area.name, prefix, strlen(prefix)) == 0) {
				fprintf(stderr, "  app_server area %d \"%s\": %zu bytes\n",
					(int)area.area, area.name, area.size);
			}
		}
	}
	_exit(2);
}

int
main(int argc, char** argv)
{
	struct sigaction action = {};
	action.sa_sigaction = segv;
	action.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &action, NULL);
	BApplication app("application/x-vnd.c2-getbitmap");
	for (int i = 1; i < argc; i++) {
		BBitmap* bitmap = BTranslationUtils::GetBitmap(argv[i]);
		if (bitmap == NULL) {
			printf("%s: no bitmap\n", argv[i]);
			continue;
		}
		BRect b = bitmap->Bounds();
		printf("%s: %d x %d, %d bytes per row\n", argv[i], (int)b.Width() + 1,
			(int)b.Height() + 1, (int)bitmap->BytesPerRow());
		delete bitmap;
	}
	return 0;
}
