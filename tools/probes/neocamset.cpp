/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * neocamset [<name>:<type>=<value>...]: shows or changes NeoCam's settings
 * file (~/config/settings/NeoCam settings, a flattened BMessage), for tests
 * without typing into its window; NeoCam reads it when it starts. Types:
 * s (string), i (int32), u (uint32; "JPEG" style codes too), b (bool, 0 or
 * 1), f (float). Build: $CXX -O2 $HAIKU_CPPFLAGS -o neocamset neocamset.cpp
 * -lbe
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>
#include <String.h>


int
main(int argc, char** argv)
{
	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	path.Append("NeoCam settings");

	BMessage settings;
	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() == B_OK)
		settings.Unflatten(&file);
	file.Unset();

	if (argc < 2) {
		settings.PrintToStream();
		return 0;
	}

	for (int i = 1; i < argc; i++) {
		BString argument(argv[i]);
		int32 colon = argument.FindFirst(':');
		int32 equals = argument.FindFirst('=');
		if (colon < 0 || equals != colon + 2) {
			fprintf(stderr, "bad argument: %s\n", argv[i]);
			return 2;
		}
		BString name, value;
		argument.CopyInto(name, 0, colon);
		argument.CopyInto(value, equals + 1, argument.Length() - equals - 1);
		char type = argument[colon + 1];
		settings.RemoveName(name);
		switch (type) {
			case 's':
				settings.AddString(name, value);
				break;
			case 'i':
				settings.AddInt32(name, atoi(value));
				break;
			case 'u':
				if (value.Length() == 4 && !isdigit(value[0])) {
					settings.AddUInt32(name, ((uint32)value[0] << 24)
						| ((uint32)value[1] << 16) | ((uint32)value[2] << 8)
						| (uint32)value[3]);
				} else
					settings.AddUInt32(name, strtoul(value, NULL, 0));
				break;
			case 'b':
				settings.AddBool(name, atoi(value) != 0);
				break;
			case 'f':
				settings.AddFloat(name, atof(value));
				break;
			default:
				fprintf(stderr, "bad type in %s\n", argv[i]);
				return 2;
		}
	}

	file.SetTo(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK || settings.Flatten(&file) != B_OK) {
		fprintf(stderr, "couldn't write %s\n", path.Path());
		return 1;
	}
	file.SetPermissions(0600);
	return 0;
}
