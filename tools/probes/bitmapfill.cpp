/* Creates app_server bitmaps of the given sizes and fills each completely,
   printing where its memory is: checks the client's mapping of the shared
   bitmap memory. Usage: bitmapfill <width>x<height>... */
#include <Application.h>
#include <Bitmap.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


static void
segv(int, siginfo_t* info, void*)
{
	fprintf(stderr, "bitmapfill: segment violation at %p\n", info->si_addr);
	_exit(2);
}


int
main(int argc, char** argv)
{
	struct sigaction action = {};
	action.sa_sigaction = segv;
	action.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &action, NULL);
	BApplication app("application/x-vnd.c2-bitmapfill");
	for (int i = 1; i < argc; i++) {
		int width, height;
		if (sscanf(argv[i], "%dx%d", &width, &height) != 2)
			continue;
		BBitmap* bitmap = new BBitmap(BRect(0, 0, width - 1, height - 1), 0,
			B_RGB32);
		printf("%dx%d: bits %p length %d area %d\n", width, height,
			bitmap->Bits(), (int)bitmap->BitsLength(), (int)bitmap->Area());
		fflush(stdout);
		memset(bitmap->Bits(), 0x55, bitmap->BitsLength());
		printf("  filled\n");
		fflush(stdout);
	}
	return 0;
}
