/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * memhog <MB> <seconds>: allocates that much memory in 4 MB pieces, writes
 * every page, keeps rewriting a page of each piece for that long, then
 * checks and frees it all: memory pressure for other tests (milestone M82 (docs/milestones.md)).
 * Exits 1 if a page lost its pattern.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: memhog <MB> <seconds>\n");
		return 2;
	}
	int pieces = atoi(argv[1]) / 4, seconds = atoi(argv[2]);
	const size_t kPiece = 4 << 20;
	uint32_t** piece = calloc(pieces, sizeof(uint32_t*));
	int got = 0;
	for (; got < pieces; got++) {
		piece[got] = malloc(kPiece);
		if (piece[got] == NULL)
			break;
		for (size_t o = 0; o < kPiece / 4; o += 1024)
			piece[got][o] = (uint32_t)(got * 2654435761u + o);
	}
	printf("holding %d MB\n", got * 4);
	fflush(stdout);
	time_t end = time(NULL) + seconds;
	while (time(NULL) < end) {
		for (int i = 0; i < got; i++)
			piece[i][(rand() % (kPiece / 4096)) * 1024] ^= 0;
	}
	long bad = 0;
	for (int i = 0; i < got; i++) {
		for (size_t o = 0; o < kPiece / 4; o += 1024)
			bad += piece[i][o] != (uint32_t)(i * 2654435761u + o);
		free(piece[i]);
	}
	printf("%ld wrong pages\n", bad);
	return bad != 0 ? 1 : 0;
}
