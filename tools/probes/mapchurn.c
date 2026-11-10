/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * mapchurn <seconds> <threads> <hold seconds>: threads map anonymous memory
 * at the kernel's (randomized) addresses, write a pattern to every page,
 * check it, sometimes make it read-only and check again, and unmap it; a
 * thread forks now and then, the child doing the same briefly. Pages of
 * other threads' regions fault in while tables around them are freed. At
 * the end it prints the counts and holds its address space for <hold>
 * seconds, for a look at the wired pages it costs (milestone M82: page
 * tables freed on unmap). Exits 1 on any wrong byte.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile int sStop;
static volatile long sMaps, sForks, sErrors;

static const size_t kSizes[] = { 16384, 65536, 1 << 20, 4 << 20 };

static uint32_t
next_random(uint32_t* state)
{
	*state ^= *state << 13;
	*state ^= *state >> 17;
	*state ^= *state << 5;
	return *state;
}


static int
churn_once(uint32_t* random, uint32_t tag)
{
	size_t size = kSizes[next_random(random) % 4];
	uint8_t* p = mmap(NULL, size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return 0;
	for (size_t o = 0; o < size; o += 4096)
		*(uint32_t*)(p + o) = tag ^ (uint32_t)o;
	int bad = 0;
	for (size_t o = 0; o < size; o += 4096)
		bad += *(uint32_t*)(p + o) != (tag ^ (uint32_t)o);
	if ((next_random(random) & 3) == 0) {
		mprotect(p, size, PROT_READ);
		for (size_t o = 0; o < size; o += 4096)
			bad += *(uint32_t*)(p + o) != (tag ^ (uint32_t)o);
	}
	munmap(p, size);
	return bad;
}


static void*
worker(void* cookie)
{
	uint32_t random = (uint32_t)(uintptr_t)cookie * 2654435761u + 7;
	long n = 0;
	while (!sStop) {
		int bad = churn_once(&random, (uint32_t)n * 2246822519u);
		if (bad != 0) {
			__sync_fetch_and_add(&sErrors, bad);
			fprintf(stderr, "mapchurn: %d wrong pages\n", bad);
		}
		__sync_fetch_and_add(&sMaps, 1);
		n++;
		if ((uintptr_t)cookie == 0 && n % 500 == 0) {
			pid_t child = fork();
			if (child == 0) {
				int childBad = 0;
				for (int i = 0; i < 50; i++)
					childBad += churn_once(&random, (uint32_t)i * 40503u);
				_exit(childBad != 0 ? 1 : 0);
			}
			if (child > 0) {
				int status;
				waitpid(child, &status, 0);
				if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
					__sync_fetch_and_add(&sErrors, 1);
				__sync_fetch_and_add(&sForks, 1);
			}
		}
	}
	return NULL;
}


int
main(int argc, char** argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: mapchurn <seconds> <threads> <hold seconds>\n");
		return 2;
	}
	int seconds = atoi(argv[1]), threads = atoi(argv[2]), hold = atoi(argv[3]);
	pthread_t thread[64];
	if (threads > 64)
		threads = 64;
	for (int i = 0; i < threads; i++)
		pthread_create(&thread[i], NULL, worker, (void*)(uintptr_t)i);
	for (int s = 1; s <= seconds; s++) {
		sleep(1);
		if (s % 30 == 0 || s == seconds) {
			printf("%d s: %ld maps, %ld forks, %ld errors\n", s, sMaps, sForks,
				sErrors);
			fflush(stdout);
		}
	}
	sStop = 1;
	for (int i = 0; i < threads; i++)
		pthread_join(thread[i], NULL);
	printf("holding\n");
	fflush(stdout);
	sleep(hold);
	return sErrors != 0 ? 1 : 0;
}
