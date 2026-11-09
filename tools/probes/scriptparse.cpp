/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * scriptparse "<command>"...: the message the Remote Control makes of each
 * command (its ScriptCommand.cpp), or why it can't, to compare with hey.
 */
#include <stdio.h>

#include <Message.h>

#include "ScriptCommand.h"


int
main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		BMessage message;
		BString error;
		printf("%s\n", argv[i]);
		if (RemoteControl::ParseScript(argv[i], message, &error) != B_OK) {
			printf("  error: %s\n", error.String());
			continue;
		}
		message.PrintToStream();
		// the specifiers, last added (resolved first) last
		BMessage specifier;
		for (int32 j = 0; message.FindMessage("specifiers", j, &specifier)
				== B_OK; j++) {
			printf("  specifier %d: %s, form %d", (int)j,
				specifier.GetString("property", "?"),
				(int)specifier.what);
			int32 index;
			if (specifier.FindInt32("index", &index) == B_OK)
				printf(", index %d", (int)index);
			const char* name;
			if (specifier.FindString("name", &name) == B_OK)
				printf(", name \"%s\"", name);
			printf("\n");
		}
	}
	return 0;
}
