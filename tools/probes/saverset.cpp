/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * Shows or sets the screen saver module in the user's ScreenSaver settings,
 * and optionally integer settings of that module, as the ScreenSaver
 * preferences do; screen_blanker reads them when it starts.
 *
 *   saverset [<module name> [<setting>=<int32>...]]
 *
 * Build: $CXX -O2 $HAIKU_CPPFLAGS -I$HAIKU_SRC/headers/private/screen_saver \
 *            -o saverset saverset.cpp -lbe -lscreensaver
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Message.h>
#include <String.h>

#include <ScreenSaverSettings.h>


int
main(int argc, char** argv)
{
	ScreenSaverSettings settings;
	settings.Load();
	if (argc > 1) {
		settings.SetModuleName(argv[1]);
		if (argc > 2) {
			BMessage state;
			settings.GetModuleState(argv[1], &state);
			for (int i = 2; i < argc; i++) {
				const char* equals = strchr(argv[i], '=');
				if (equals == NULL)
					continue;
				BString name(argv[i], equals - argv[i]);
				state.RemoveName(name.String());
				state.AddInt32(name.String(), atoi(equals + 1));
			}
			settings.SetModuleState(argv[1], &state);
		}
		settings.Save();
	}

	printf("%s\n", settings.ModuleName());
	BMessage state;
	if (settings.GetModuleState(settings.ModuleName(), &state) == B_OK)
		state.PrintToStream();
	return 0;
}
