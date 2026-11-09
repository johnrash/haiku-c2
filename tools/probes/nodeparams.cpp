/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * nodeparams <node id> [<name> <value>]: the parameters of a live media node
 * (its parameter web, as Media preferences shows it) with their current
 * values: gains, mutes, rates. With a name and an integer value, sets that
 * discrete parameter first, as choosing it in Media preferences does (a
 * multi_audio output frequency: 128 for 44100 Hz, 256 for 48000 Hz).
 */
#include <MediaRoster.h>
#include <ParameterWeb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: nodeparams <node id>\n");
		return 1;
	}
	BMediaRoster* roster = BMediaRoster::Roster();
	media_node node;
	if (roster == NULL || roster->GetNodeFor(atoi(argv[1]), &node) != B_OK) {
		fprintf(stderr, "no node %s\n", argv[1]);
		return 1;
	}
	BParameterWeb* web = NULL;
	if (roster->GetParameterWebFor(node, &web) != B_OK || web == NULL) {
		fprintf(stderr, "node %s has no parameters\n", argv[1]);
		return 1;
	}
	if (argc >= 4) {
		for (int32 i = 0; i < web->CountParameters(); i++) {
			BParameter* parameter = web->ParameterAt(i);
			if (parameter->Type() != BParameter::B_DISCRETE_PARAMETER
				|| strcmp(parameter->Name(), argv[2]) != 0) {
				continue;
			}
			int32 value = atoi(argv[3]);
			status_t status = parameter->SetValue(&value, sizeof(value), 0);
			printf("set %s to %" B_PRId32 ": %s\n", argv[2], value,
				strerror(status));
			snooze(500000);
			break;
		}
	}
	for (int32 i = 0; i < web->CountParameters(); i++) {
		BParameter* parameter = web->ParameterAt(i);
		if (parameter->Type() == BParameter::B_NULL_PARAMETER)
			continue;
		char value[64];
		size_t size = sizeof(value);
		bigtime_t when;
		if (parameter->GetValue(value, &size, &when) != B_OK) {
			printf("%-32s (no value)\n", parameter->Name());
			continue;
		}
		printf("%-32s %s:", parameter->Name(),
			parameter->Type() == BParameter::B_CONTINUOUS_PARAMETER
				? "continuous" : "discrete");
		if (parameter->Type() == BParameter::B_CONTINUOUS_PARAMETER) {
			for (size_t k = 0; k < size / sizeof(float); k++)
				printf(" %.2f", ((float*)value)[k]);
		} else
			printf(" %" B_PRId32, *(int32*)value);
		printf("\n");
	}
	delete web;
	return 0;
}
