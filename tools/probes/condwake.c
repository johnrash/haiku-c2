/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * condwake: how soon four waiting threads start once another thread signals
 * them, the way a fork-join software renderer does every
 * frame, and how long after their 3 ms of work the signalling thread is back.
 *
 *   condwake m          one mutex and pthread_cond_broadcast()
 *   condwake s          a semaphore per thread, release_sem()
 *   condwake sn         the same with B_DO_NOT_RESCHEDULE
 *   condwake <mode> pin each thread on its own CPU
 *
 * On the ODROID-C2 (2026-10-30), start delays of the four threads:
 *   m   100-270 us each, 0.6 ms lost a round
 *   s   10, 250, 550, 950 us: each release_sem() took 120-560 us, the
 *       releasing thread giving up its CPU to the woken one
 *   sn  10-44 us, 56 us lost a round; release_sem_etc() 3-4 us
 *
 * Build: $CC -O2 $HAIKU_CPPFLAGS -o condwake condwake.c
 */


#include <OS.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern status_t _kern_set_thread_affinity(thread_id id, const void* userMask, size_t size);
static int sPin;
static void pin(int cpu) { uint32 mask[8]; memset(mask, 0, sizeof(mask)); mask[0] = 1u << cpu; _kern_set_thread_affinity(find_thread(NULL), mask, sizeof(mask)); }

#define N 4
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sStart = PTHREAD_COND_INITIALIZER;
static pthread_cond_t sDone = PTHREAD_COND_INITIALIZER;
static int sGeneration, sPending, sQuit;
static bigtime_t sSignal, sLag[N], sDoneLag;
static sem_id sSems[N], sDoneSem;
static int sUseSems, sNoResched;
static bigtime_t sReleaseTime[N];
#define MAX_ROUNDS 300
static bigtime_t sRoundLag[N][MAX_ROUNDS];
static int sRound;
static bigtime_t sRoundSignal[MAX_ROUNDS];
static thread_id sThreadIDs[N];

static int
compare(const void* a, const void* b)
{
	bigtime_t x = *(const bigtime_t*)a, y = *(const bigtime_t*)b;
	return x < y ? -1 : x > y;
}

static void*
worker(void* data)
{
	int me = (int)(long)data, generation = 0;
	sThreadIDs[me] = find_thread(NULL);
	if (sPin) pin(me);
	for (;;) {
		if (sUseSems) {
			acquire_sem(sSems[me]);
			if (sQuit) break;
		} else {
			pthread_mutex_lock(&sLock);
			while (sGeneration == generation && !sQuit)
				pthread_cond_wait(&sStart, &sLock);
			if (sQuit) { pthread_mutex_unlock(&sLock); break; }
			generation = sGeneration;
			pthread_mutex_unlock(&sLock);
		}
		bigtime_t lag = system_time() - sSignal;
		sLag[me] += lag;
		sRoundLag[me][sRound] = lag;
		bigtime_t end = system_time() + 3000;	/* 3 ms of work */
		while (system_time() < end)
			;
		if (sUseSems)
			release_sem(sDoneSem);
		else {
			pthread_mutex_lock(&sLock);
			if (--sPending == 0) pthread_cond_signal(&sDone);
			pthread_mutex_unlock(&sLock);
		}
	}
	return NULL;
}

int
main(int argc, char** argv)
{
	int rounds = 300;
	sUseSems = argc > 1 && argv[1][0] == 's';
	sPin = argc > 2 && argv[2][0] == 'p';
	sNoResched = argc > 1 && argv[1][1] == 'n';
	pthread_t threads[N];
	for (int i = 0; i < N; i++) {
		sSems[i] = create_sem(0, "start");
		pthread_create(&threads[i], NULL, worker, (void*)(long)i);
	}
	sDoneSem = create_sem(0, "done");
	snooze(10000);
	bigtime_t doneLag = 0;
	for (int r = 0; r < rounds; r++) {
		snooze(5000);
		sRound = r;
		sSignal = system_time();
		sRoundSignal[r] = sSignal;
		if (sUseSems) {
			for (int i = 0; i < N; i++) { bigtime_t t = system_time(); if (sNoResched) release_sem_etc(sSems[i], 1, B_DO_NOT_RESCHEDULE); else release_sem(sSems[i]); sReleaseTime[i] += system_time() - t; }
			acquire_sem_etc(sDoneSem, N, 0, 0);
		} else {
			pthread_mutex_lock(&sLock);
			sPending = N; sGeneration++;
			pthread_cond_broadcast(&sStart);
			pthread_mutex_unlock(&sLock);
			pthread_mutex_lock(&sLock);
			while (sPending > 0) pthread_cond_wait(&sDone, &sLock);
			pthread_mutex_unlock(&sLock);
		}
		doneLag += system_time() - sSignal - 3000;
	}
	printf("%s%s%s: start delay", sUseSems ? "semaphores" : "mutex+cond", sNoResched ? " (no reschedule)" : "", sPin ? ", pinned" : "");
	for (int i = 0; i < N; i++) printf(" %.0f", (double)sLag[i] / rounds);
	printf(" us; signal to all done minus the 3 ms of work: %.0f us\n", (double)doneLag / rounds);
	if (getenv("CONDWAKE_SLOW") != NULL) {
		printf("  threads %d %d %d %d; main %d; rounds with a start over 1 ms:\n",
			(int)sThreadIDs[0], (int)sThreadIDs[1], (int)sThreadIDs[2],
			(int)sThreadIDs[3], (int)find_thread(NULL));
		for (int r = 0; r < rounds; r++) {
			int slow = 0;
			for (int i = 0; i < N; i++)
				slow |= sRoundLag[i][r] > 1000;
			if (slow) {
				printf("    round %d at %lld:", r, (long long)sRoundSignal[r]);
				for (int i = 0; i < N; i++)
					printf(" %lld", (long long)sRoundLag[i][r]);
				printf("\n");
			}
		}
	}
	printf("  start delay median/90%%/max:");
	for (int i = 0; i < N; i++) {
		bigtime_t sorted[MAX_ROUNDS];
		memcpy(sorted, sRoundLag[i], sizeof(sorted));
		qsort(sorted, rounds, sizeof(bigtime_t), compare);
		printf(" %lld/%lld/%lld", (long long)sorted[rounds / 2],
			(long long)sorted[rounds * 9 / 10],
			(long long)sorted[rounds - 1]);
	}
	printf(" us\n");
	if (sUseSems) { printf("  release_sem() took"); for (int i = 0; i < N; i++) printf(" %.0f", (double)sReleaseTime[i] / rounds); printf(" us\n"); }
	return 0;
}
