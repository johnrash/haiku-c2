/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * pingpong: request/reply round trips between two threads, the pattern of
 * a BLooper message and its reply, where waking the other thread on the
 * same CPU is cheapest. Measures the scheduler's choice of CPU for a woken
 * thread (M25 item 7, the release_sem() stall).
 *
 *   pingpong s [<rounds>]   two semaphores, release_sem() then acquire_sem()
 *   pingpong p [<rounds>]   two ports, write_port() then read_port()
 *   pingpong t [<rounds>]   ports to a second team (fork())
 *
 * Prints the median, 90th percentile and maximum round trip in us.
 *
 * Build: $CC -O2 $HAIKU_CPPFLAGS -o pingpong pingpong.c
 */


#include <OS.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


static sem_id sPing, sPong;
static port_id sPingPort, sPongPort;
static int sRounds = 20000;


static status_t
sem_echo(void* data)
{
	for (int i = 0; i < sRounds; i++) {
		acquire_sem(sPing);
		release_sem(sPong);
	}
	return B_OK;
}


static status_t
port_echo(void* data)
{
	int32 code;
	char buffer[16];
	for (int i = 0; i < sRounds; i++) {
		read_port(sPingPort, &code, buffer, sizeof(buffer));
		write_port(sPongPort, code, buffer, 8);
	}
	return B_OK;
}


static int
compare(const void* a, const void* b)
{
	bigtime_t x = *(const bigtime_t*)a, y = *(const bigtime_t*)b;
	return x < y ? -1 : x > y;
}


int
main(int argc, char** argv)
{
	char mode = argc > 1 ? argv[1][0] : 's';
	if (argc > 2)
		sRounds = atoi(argv[2]);
	bigtime_t* times = malloc(sRounds * sizeof(bigtime_t));

	sPing = create_sem(0, "ping");
	sPong = create_sem(0, "pong");
	sPingPort = create_port(1, "ping");
	sPongPort = create_port(1, "pong");

	thread_id thread = -1;
	pid_t child = -1;
	if (mode == 't') {
		child = fork();
		if (child == 0) {
			port_echo(NULL);
			_exit(0);
		}
	} else {
		thread = spawn_thread(mode == 's' ? sem_echo : port_echo, "echo",
			B_NORMAL_PRIORITY, NULL);
		resume_thread(thread);
	}
	snooze(10000);

	int32 code;
	char buffer[16];
	memset(buffer, 0, sizeof(buffer));
	for (int i = 0; i < sRounds; i++) {
		bigtime_t start = system_time();
		if (mode == 's') {
			release_sem(sPing);
			acquire_sem(sPong);
		} else {
			write_port(sPingPort, i, buffer, 8);
			read_port(sPongPort, &code, buffer, sizeof(buffer));
		}
		times[i] = system_time() - start;
		if ((i & 255) == 255)
			snooze(500);
				// sleep now and then, as real request/reply traffic does
	}
	if (child > 0)
		waitpid(child, NULL, 0);
	else {
		status_t result;
		wait_for_thread(thread, &result);
	}

	qsort(times, sRounds, sizeof(bigtime_t), compare);
	printf("%s: %d round trips: median %lld us, 90%% %lld us, max %lld us\n",
		mode == 's' ? "semaphores" : mode == 'p' ? "ports" : "ports to a team",
		sRounds, (long long)times[sRounds / 2],
		(long long)times[sRounds * 9 / 10], (long long)times[sRounds - 1]);
	return 0;
}
