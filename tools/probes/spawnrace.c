/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * spawnrace [<count>]: posix_spawn() of /bin/true <count> times (default
 * 1000) through its fork path (POSIX_SPAWN_SETSID, as net_server starts its
 * services) while eight other threads keep malloc() busy, as net_server's
 * DHCP and device threads can at that moment (with 8 heap pools, the 8th
 * thread to use malloc() shares the main thread's). A vfork()ed child that copied
 * a held heap lock never gets to its exec(): the spawn does not return. A
 * spawn that takes over 5 s counts as hung; the stuck child is then killed,
 * and spawnrace exits 1.
 */
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <OS.h>


#define CHURN_THREADS 8

static volatile int32 sDone;
static int32 sSpawned;


static status_t
churn(void* data)
{
	uint32 seed = (uint32)(addr_t)data;
	void* blocks[64] = {};
	while (!sDone) {
		seed = seed * 1103515245 + 12345;
		int slot = (seed >> 8) % 64;
		free(blocks[slot]);
		blocks[slot] = malloc(16 + (seed >> 16) % 4000);
	}
	for (int i = 0; i < 64; i++)
		free(blocks[i]);
	return 0;
}


static status_t
watchdog(void* data)
{
	int32 last = -1;
	bigtime_t since = system_time();
	while (!sDone) {
		snooze(100000);
		int32 now = atomic_get(&sSpawned);
		if (now != last) {
			last = now;
			since = system_time();
			continue;
		}
		if (system_time() - since < 5000000)
			continue;

		// the stuck child: our child team that is still called like us
		printf("spawnrace: spawn %d hung for 5 s\n", (int)now + 1);
		team_info info;
		int32 cookie = 0;
		while (get_next_team_info(&cookie, &info) == B_OK) {
			if (info.parent == getpid()) {
				printf("spawnrace: killing child team %d (%s)\n",
					(int)info.team, info.args);
				kill(info.team, SIGKILL);
			}
		}
		fflush(stdout);
		_exit(1);
	}
	return 0;
}


int
main(int argc, char** argv)
{
	int count = argc > 1 ? atoi(argv[1]) : 1000;
	thread_id threads[CHURN_THREADS];
	for (int i = 0; i < CHURN_THREADS; i++) {
		threads[i] = spawn_thread(churn, "churn", B_NORMAL_PRIORITY,
			(void*)(addr_t)(i + 1));
		resume_thread(threads[i]);
	}
	resume_thread(spawn_thread(watchdog, "watchdog", B_NORMAL_PRIORITY,
		NULL));

	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
	char* args[] = { "/bin/true", NULL };
	extern char** environ;
	bigtime_t start = system_time();
	int failed = 0;
	for (int i = 0; i < count; i++) {
		pid_t pid;
		if (posix_spawn(&pid, args[0], NULL, &attr, args, environ) != 0) {
			failed++;
			continue;
		}
		int status;
		waitpid(pid, &status, 0);
		atomic_add(&sSpawned, 1);
	}
	sDone = 1;
	for (int i = 0; i < CHURN_THREADS; i++) {
		status_t result;
		wait_for_thread(threads[i], &result);
	}
	printf("spawnrace: %d spawns in %.1f s, %d failed, none hung\n",
		(int)atomic_get(&sSpawned), (system_time() - start) / 1000000.0,
		failed);
	return failed == 0 ? 0 : 1;
}
