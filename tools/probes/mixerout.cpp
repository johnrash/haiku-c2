/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * mixerout: where the system mixer's output goes (node and name), and what
 * feeds it, as the media server has it connected now.
 */
#include <MediaRoster.h>
#include <MediaAddOn.h>

#include <stdio.h>
#include <string.h>


static live_node_info sNodes[64];
static int32 sNodeCount;


static const char*
node_at_port(port_id port, media_node_id* _id)
{
	for (int32 i = 0; i < sNodeCount; i++) {
		if (sNodes[i].node.port == port) {
			*_id = sNodes[i].node.node;
			return sNodes[i].name;
		}
	}
	*_id = -1;
	return "?";
}


int
main()
{
	status_t status;
	BMediaRoster* roster = BMediaRoster::Roster(&status);
	if (roster == NULL) {
		fprintf(stderr, "no media roster: %s\n", strerror(status));
		return 1;
	}
	media_node mixer;
	if (roster->GetAudioMixer(&mixer) != B_OK) {
		fprintf(stderr, "no audio mixer\n");
		return 1;
	}
	sNodeCount = 64;
	roster->GetLiveNodes(sNodes, &sNodeCount);
	media_node_id id;
	media_output outputs[8];
	int32 count = 0;
	roster->GetConnectedOutputsFor(mixer, outputs, 8, &count);
	for (int32 i = 0; i < count; i++) {
		const char* name = node_at_port(outputs[i].destination.port, &id);
		printf("mixer output \"%s\" -> node %" B_PRId32 " %s, %.0f Hz\n",
			outputs[i].name, id, name,
			outputs[i].format.u.raw_audio.frame_rate);
	}
	media_input inputs[16];
	count = 0;
	roster->GetConnectedInputsFor(mixer, inputs, 16, &count);
	for (int32 i = 0; i < count; i++) {
		const char* name = node_at_port(inputs[i].source.port, &id);
		printf("mixer input \"%s\" <- node %" B_PRId32 " %s\n", inputs[i].name,
			id, name);
	}
	return 0;
}
