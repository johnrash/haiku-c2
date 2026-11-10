/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 *   keyframes <file> [seconds]
 *
 * The video track's keyframes in the first <seconds> (default 300), as the
 * Media Kit finds them (BMediaTrack::FindKeyFrameForTime, backward, every
 * 0.25 s), and the gaps between them: MediaPlayer snaps its seeks to them
 * (milestone M71).
 *
 * Build: $CXX -O2 -o keyframes keyframes.cpp -lmedia -lbe
 */


#include <stdio.h>
#include <stdlib.h>

#include <Entry.h>
#include <MediaFile.h>
#include <MediaTrack.h>


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: keyframes <file> [seconds]\n");
		return 2;
	}
	double seconds = argc > 2 ? atof(argv[2]) : 300;
	entry_ref ref;
	if (get_ref_for_path(argv[1], &ref) != B_OK) {
		fprintf(stderr, "keyframes: no file %s\n", argv[1]);
		return 1;
	}
	BMediaFile file(&ref);
	if (file.InitCheck() != B_OK) {
		fprintf(stderr, "keyframes: %s\n", strerror(file.InitCheck()));
		return 1;
	}
	BMediaTrack* video = NULL;
	for (int32 i = 0; i < file.CountTracks(); i++) {
		BMediaTrack* track = file.TrackAt(i);
		media_format format;
		if (track->EncodedFormat(&format) == B_OK
			&& format.type == B_MEDIA_ENCODED_VIDEO) {
			video = track;
			break;
		}
		file.ReleaseTrack(track);
	}
	if (video == NULL) {
		fprintf(stderr, "keyframes: no video track\n");
		return 1;
	}
	printf("%s: %.1f s, %lld frames\n", argv[1], video->Duration() / 1e6,
		(long long)video->CountFrames());

	bigtime_t last = -1;
	bigtime_t previous = -1;
	bigtime_t longest = 0;
	int count = 0;
	double gaps = 0;
	for (bigtime_t t = 0; t <= (bigtime_t)(seconds * 1e6); t += 250000) {
		bigtime_t key = t;
		if (video->FindKeyFrameForTime(&key, B_MEDIA_SEEK_CLOSEST_BACKWARD)
				!= B_OK || key == last) {
			continue;
		}
		if (previous >= 0) {
			bigtime_t gap = key - previous;
			longest = gap > longest ? gap : longest;
			gaps += gap;
		}
		if (count < 12)
			printf("  keyframe at %.2f s\n", key / 1e6);
		previous = last = key;
		count++;
	}
	printf("%d keyframes in the first %.0f s: %.2f s apart on average, "
		"%.2f s at most\n", count, seconds,
		count > 1 ? gaps / (count - 1) / 1e6 : 0.0, longest / 1e6);
	return 0;
}
