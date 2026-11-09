/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * rwlocksignal: whether libroot's rw_lock (BLooperList's lock, and so every
 * application's) survives a signal to a thread waiting for it. A thread
 * waits for a read lock while this one holds the write lock; a signal
 * interrupts its wait; then the write lock is released. The reader must
 * get the lock and finish, and not spin: with the bug, the kernel kept
 * B_INTERRUPTED as its wait status, so the release's wake-up was dropped
 * and the reader spun forever, holding a reader's place in the lock (the
 * frozen MediaPlayer of 2026-10-31).
 * Also checks pthread_rwlock the same way.
 */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>

#include <OS.h>

#include <locks.h>


static rw_lock sLock = RW_LOCK_INITIALIZER("rwlocksignal");
static pthread_rwlock_t sPosixLock = PTHREAD_RWLOCK_INITIALIZER;
static int32 sGotIt = 0;


static void
handler(int)
{
}


static status_t
reader(void*)
{
	rw_lock_read_lock(&sLock);
	atomic_set(&sGotIt, 1);
	rw_lock_read_unlock(&sLock);
	return B_OK;
}


static status_t
posix_reader(void*)
{
	pthread_rwlock_rdlock(&sPosixLock);
	atomic_set(&sGotIt, 1);
	pthread_rwlock_unlock(&sPosixLock);
	return B_OK;
}


static bool
run(const char* name, thread_func function, void (*lock)(), void (*unlock)())
{
	atomic_set(&sGotIt, 0);
	lock();
	thread_id thread = spawn_thread(function, name, B_NORMAL_PRIORITY, NULL);
	resume_thread(thread);
	snooze(200000);
		// it waits for the lock now
	send_signal(thread, SIGUSR1);
	snooze(200000);
	thread_info before;
	get_thread_info(thread, &before);
	snooze(500000);
	thread_info after;
	get_thread_info(thread, &after);
	bigtime_t spun = after.user_time + after.kernel_time - before.user_time
		- before.kernel_time;
	unlock();

	status_t result;
	status_t status = wait_for_thread_etc(thread, B_RELATIVE_TIMEOUT, 2000000,
		&result);
	bool ok = status == B_OK && atomic_get(&sGotIt) == 1;
	printf("%s: %s; while interrupted it used %d ms of CPU in 500 ms\n", name,
		ok ? "got the lock after the release"
			: "STUCK: the release's wake-up was lost", (int)(spun / 1000));
	if (!ok)
		kill_thread(thread);
	return ok;
}


int
main()
{
	signal(SIGUSR1, &handler);
	bool ok = run("rw_lock", &reader,
		[]() { rw_lock_write_lock(&sLock); },
		[]() { rw_lock_write_unlock(&sLock); });
	ok &= run("pthread_rwlock", &posix_reader,
		[]() { pthread_rwlock_wrlock(&sPosixLock); },
		[]() { pthread_rwlock_unlock(&sPosixLock); });
	return ok ? 0 : 1;
}
