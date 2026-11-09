/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * audioout [<name>|#<n>]: lists the physical audio outputs the media server
 * knows (numbered from 1) and the default one; with a name (as listed) or a
 * number (two adapters may have the same name), makes that output the
 * default, as Media preferences does (BMediaRoster::SetAudioOutput()).
 * Restart the media services afterwards for the system mixer to follow.
 */
#include <MediaRoster.h>
#include <MediaAddOn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int
main(int argc, char** argv)
{
	status_t status;
	BMediaRoster* roster = BMediaRoster::Roster(&status);
	if (roster == NULL) {
		fprintf(stderr, "no media roster: %s\n", strerror(status));
		return 1;
	}

	media_node current;
	live_node_info currentInfo;
	bool haveCurrent = roster->GetAudioOutput(&current) == B_OK
		&& roster->GetLiveNodeInfo(current, &currentInfo) == B_OK;
	if (haveCurrent)
		printf("default: %s\n", currentInfo.name);

	live_node_info nodes[64];
	int32 count = 64;
	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	status = roster->GetLiveNodes(nodes, &count, &format, NULL, NULL,
		B_PHYSICAL_OUTPUT);
	if (status != B_OK) {
		fprintf(stderr, "can't list outputs: %s\n", strerror(status));
		return 1;
	}
	int found = -1;
	int wanted = argc > 1 && argv[1][0] == '#' ? atoi(argv[1] + 1) : 0;
	for (int32 i = 0; i < count; i++) {
		printf("output %" B_PRId32 ": %s (node %" B_PRId32 ")\n", i + 1,
			nodes[i].name, nodes[i].node.node);
		if (argc > 1 && (wanted > 0 ? wanted == i + 1
				: strcmp(nodes[i].name, argv[1]) == 0)) {
			found = i;
		}
	}
	if (argc < 2)
		return 0;
	if (found < 0) {
		fprintf(stderr, "no output named \"%s\"\n", argv[1]);
		return 1;
	}
	status = roster->SetAudioOutput(nodes[found].node);
	printf("set default to %s: %s\n", nodes[found].name, strerror(status));
	return status == B_OK ? 0 : 1;
}
