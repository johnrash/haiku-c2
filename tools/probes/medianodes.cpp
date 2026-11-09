/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * medianodes: lists the media_server's live and dormant nodes and its
 * defaults, sets the default video or audio input, or lists or sets a
 * node's discrete (int32) and continuous (float) parameters.
 * Usage: medianodes [video-input <node id>]
 *        medianodes audio-input <node id>
 *        medianodes params <node id>
 *        medianodes set-param <node id> <parameter name> <value>
 */

#include <MediaAddOn.h>
#include <MediaRoster.h>
#include <ParameterWeb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void
print_default(const char* label, status_t (BMediaRoster::*get)(media_node*))
{
	media_node node;
	status_t status = (BMediaRoster::Roster()->*get)(&node);
	if (status != B_OK) {
		printf("%-14s none (%s)\n", label, strerror(status));
		return;
	}
	live_node_info info;
	if (BMediaRoster::Roster()->GetLiveNodeInfo(node, &info) == B_OK)
		printf("%-14s %d \"%s\"\n", label, (int)node.node, info.name);
	else
		printf("%-14s %d\n", label, (int)node.node);
	BMediaRoster::Roster()->ReleaseNode(node);
}


int
main(int argc, char** argv)
{
	status_t status;
	BMediaRoster* roster = BMediaRoster::Roster(&status);
	if (roster == NULL) {
		printf("no roster: %s\n", strerror(status));
		return 1;
	}

	if ((argc == 3 && strcmp(argv[1], "params") == 0)
		|| (argc == 5 && strcmp(argv[1], "set-param") == 0)) {
		media_node node;
		status = roster->GetNodeFor(atoi(argv[2]), &node);
		BParameterWeb* web = NULL;
		if (status == B_OK)
			status = roster->GetParameterWebFor(node, &web);
		if (status != B_OK) {
			printf("parameters of node %s: %s\n", argv[2], strerror(status));
			return 1;
		}
		bool found = false;
		for (int32 i = 0; i < web->CountParameters(); i++) {
			BParameter* parameter = web->ParameterAt(i);
			bool discrete
				= parameter->Type() == BParameter::B_DISCRETE_PARAMETER;
			if (!discrete
				&& parameter->Type() != BParameter::B_CONTINUOUS_PARAMETER) {
				continue;
			}
			if (argc == 5) {
				if (strcmp(parameter->Name(), argv[3]) != 0)
					continue;
				int32 intValue = atoi(argv[4]);
				float floatValue = atof(argv[4]);
				status = discrete
					? parameter->SetValue(&intValue, sizeof(intValue),
						system_time())
					: parameter->SetValue(&floatValue, sizeof(floatValue),
						system_time());
				printf("%s = %s: %s\n", argv[3], argv[4], strerror(status));
				found = true;
			}
			union {
				int32 i;
				float f;
			} value;
			size_t size = sizeof(value);
			bigtime_t when;
			if (parameter->GetValue(&value, &size, &when) == B_OK) {
				if (discrete)
					printf("  \"%s\": %d\n", parameter->Name(), (int)value.i);
				else
					printf("  \"%s\": %g\n", parameter->Name(), value.f);
			}
		}
		delete web;
		roster->ReleaseNode(node);
		return argc == 5 && !found ? 1 : 0;
	}

	if (argc == 3 && (strcmp(argv[1], "video-input") == 0
			|| strcmp(argv[1], "audio-input") == 0)) {
		bool video = strcmp(argv[1], "video-input") == 0;
		media_node node;
		status = roster->GetNodeFor(atoi(argv[2]), &node);
		if (status == B_OK) {
			status = video ? roster->SetVideoInput(node)
				: roster->SetAudioInput(node);
			roster->ReleaseNode(node);
		}
		printf("Set%sInput(%s): %s\n", video ? "Video" : "Audio", argv[2],
			strerror(status));
		return status == B_OK ? 0 : 1;
	}

	live_node_info live[64];
	int32 count = 64;
	roster->GetLiveNodes(live, &count);
	printf("live nodes: %d\n", (int)count);
	for (int32 i = 0; i < count; i++) {
		printf("  %3d kinds 0x%06llx \"%s\"\n", (int)live[i].node.node,
			(unsigned long long)live[i].node.kind, live[i].name);
	}

	media_format format;
	format.type = B_MEDIA_RAW_VIDEO;
	count = 64;
	roster->GetLiveNodes(live, &count, NULL, &format, NULL,
		B_BUFFER_PRODUCER | B_PHYSICAL_INPUT);
	printf("raw video physical inputs: %d\n", (int)count);
	for (int32 i = 0; i < count; i++)
		printf("  %3d \"%s\"\n", (int)live[i].node.node, live[i].name);

	dormant_node_info dormant[64];
	count = 64;
	roster->GetDormantNodes(dormant, &count);
	printf("dormant nodes: %d\n", (int)count);
	for (int32 i = 0; i < count; i++) {
		printf("  add-on %d flavor %d \"%s\"\n", (int)dormant[i].addon,
			(int)dormant[i].flavor_id, dormant[i].name);
	}

	print_default("video input", &BMediaRoster::GetVideoInput);
	print_default("video output", &BMediaRoster::GetVideoOutput);
	print_default("audio input", &BMediaRoster::GetAudioInput);
	print_default("audio output", &BMediaRoster::GetAudioOutput);
	return 0;
}
