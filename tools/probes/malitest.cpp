/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * malitest: the meson_mali driver's lima interface without Mesa (docs/milestones.md
 * M43): parameters, contexts, buffers (create, info, clone, write, read,
 * close), a heap buffer, sync objects, buffer waits, rejected submits, and
 * 1000 buffers made and closed (the GPU address space reused). Exit status
 * 0 if all passed.
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <OS.h>

#include <meson_mali.h>


static int sFailures;


static void
check(bool ok, const char* what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		sFailures++;
}


static int
control(int fd, uint32 request, void* arg)
{
	int result = ioctl(fd, request, arg, (size_t)((request >> 16) & 0x3fff));
	return result < 0 ? errno : 0;
}


int
main()
{
	int fd = open(MESON_MALI_DEVICE, O_RDWR);
	if (fd < 0) {
		perror(MESON_MALI_DEVICE);
		return 1;
	}

	mali_get_param param = { MALI_PARAM_GPU_ID, 0, 0 };
	check(control(fd, MALI_IOCTL_GET_PARAM, &param) == 0
		&& param.value == MALI_GPU_ID_MALI450, "GPU_ID is Mali-450");
	param.param = MALI_PARAM_NUM_PP;
	check(control(fd, MALI_IOCTL_GET_PARAM, &param) == 0 && param.value == 3,
		"NUM_PP is 3");
	param.param = MALI_PARAM_GP_VERSION;
	control(fd, MALI_IOCTL_GET_PARAM, &param);
	printf("     GP version %#llx\n", (unsigned long long)param.value);
	param.param = 99;
	check(control(fd, MALI_IOCTL_GET_PARAM, &param) != 0,
		"an unknown parameter is refused");

	mali_ctx_create context = { 0, 0 };
	check(control(fd, MALI_IOCTL_CTX_CREATE, &context) == 0, "CTX_CREATE");
	mali_ctx_free freeContext = { context.id, 0 };
	check(control(fd, MALI_IOCTL_CTX_FREE, &freeContext) == 0, "CTX_FREE");

	// buffers
	mali_gem_create create = { 100000, 0, 0, 0 };
	check(control(fd, MALI_IOCTL_GEM_CREATE, &create) == 0
		&& create.handle != 0, "GEM_CREATE 100000 bytes");
	mali_gem_info info = { create.handle, 0, 0 };
	check(control(fd, MALI_IOCTL_GEM_INFO, &info) == 0 && info.va != 0
		&& info.va % B_PAGE_SIZE == 0, "GEM_INFO: an address");
	printf("     handle %u at GPU address %#x, area %lld\n", create.handle,
		info.va, (long long)info.offset);
	void* address;
	area_id clone = clone_area("malitest", &address, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, (area_id)info.offset);
	check(clone >= 0, "the buffer's area clones");
	if (clone >= 0) {
		uint32* words = (uint32*)address;
		bool zero = true;
		for (uint32 i = 0; i < 100000 / 4; i++)
			zero &= words[i] == 0;
		check(zero, "a new buffer is all zeros");
		for (uint32 i = 0; i < 100000 / 4; i++)
			words[i] = i * 2654435761u;
		bool same = true;
		for (uint32 i = 0; i < 100000 / 4; i++)
			same &= words[i] == i * 2654435761u;
		check(same, "written and read back through the clone");
	}

	mali_gem_create second = { 4096, 0, 0, 0 };
	control(fd, MALI_IOCTL_GEM_CREATE, &second);
	mali_gem_info secondInfo = { second.handle, 0, 0 };
	control(fd, MALI_IOCTL_GEM_INFO, &secondInfo);
	check(secondInfo.va >= info.va + 102400 || secondInfo.va + 4096 <= info.va,
		"two buffers don't overlap on the GPU");

	// a heap buffer: addresses for all of it, an area only the GPU uses
	mali_gem_create heap = { 16 * 1024 * 1024, MALI_BO_FLAG_HEAP, 0, 0 };
	check(control(fd, MALI_IOCTL_GEM_CREATE, &heap) == 0 && heap.handle != 0,
		"GEM_CREATE a heap of 16 MiB");
	mali_gem_info heapInfo = { heap.handle, 0, 0 };
	check(control(fd, MALI_IOCTL_GEM_INFO, &heapInfo) == 0
		&& heapInfo.va != 0, "GEM_INFO: the heap's address");
	void* heapAddress;
	area_id heapClone = clone_area("malitest heap", &heapAddress,
		B_ANY_ADDRESS, B_READ_AREA, (area_id)heapInfo.offset);
	check(heapClone < 0, "the heap's area doesn't clone");
	if (heapClone >= 0)
		delete_area(heapClone);
	mali_gem_create after = { 4096, 0, 0, 0 };
	control(fd, MALI_IOCTL_GEM_CREATE, &after);
	mali_gem_info afterInfo = { after.handle, 0, 0 };
	control(fd, MALI_IOCTL_GEM_INFO, &afterInfo);
	check(afterInfo.va >= heapInfo.va + 16 * 1024 * 1024
		|| afterInfo.va + 4096 <= heapInfo.va,
		"a buffer made after it is outside the heap's 16 MiB");
	mali_gem_close heapClose = { heap.handle, 0 };
	check(control(fd, MALI_IOCTL_GEM_CLOSE, &heapClose) == 0,
		"GEM_CLOSE the heap");
	heapClose.handle = after.handle;
	control(fd, MALI_IOCTL_GEM_CLOSE, &heapClose);

	mali_gem_wait wait = { create.handle, MALI_GEM_WAIT_WRITE, 0 };
	check(control(fd, MALI_IOCTL_GEM_WAIT, &wait) == 0,
		"GEM_WAIT on an idle buffer");

	// sync objects
	mali_syncobj_create syncobj = { 0, MALI_SYNCOBJ_CREATE_SIGNALED };
	check(control(fd, MALI_IOCTL_SYNCOBJ_CREATE, &syncobj) == 0
		&& syncobj.handle != 0, "SYNCOBJ_CREATE");
	uint32 handles[1] = { syncobj.handle };
	mali_syncobj_wait syncWait = { (uint64)(addr_t)handles,
		(int64)system_time() * 1000 + 1000000000, 1, 0, 0, 0 };
	check(control(fd, MALI_IOCTL_SYNCOBJ_WAIT, &syncWait) == 0,
		"SYNCOBJ_WAIT on a signalled object");

	// submits the driver must refuse
	mali_gem_submit_bo objects[1] = { { create.handle, MALI_SUBMIT_BO_READ } };
	mali_gp_frame frame;
	memset(&frame, 0, sizeof(frame));
	mali_gem_submit submit;
	memset(&submit, 0, sizeof(submit));
	submit.ctx = context.id;
	submit.pipe = MALI_PIPE_GP;
	submit.nr_bos = 1;
	submit.frame_size = sizeof(frame);
	submit.bos = (uint64)(addr_t)objects;
	submit.frame = (uint64)(addr_t)&frame;
	submit.out_sync = syncobj.handle;
	check(control(fd, MALI_IOCTL_GEM_SUBMIT, &submit) != 0,
		"a GP frame with both lists empty is refused");
	submit.frame_size = 20;
	check(control(fd, MALI_IOCTL_GEM_SUBMIT, &submit) != 0,
		"a wrong frame size is refused");
	submit.frame_size = sizeof(frame);
	objects[0].handle = 12345;
	frame.frame[1] = 0x1000;
	check(control(fd, MALI_IOCTL_GEM_SUBMIT, &submit) != 0,
		"an unknown buffer is refused");

	mali_syncobj_destroy destroy = { syncobj.handle, 0 };
	check(control(fd, MALI_IOCTL_SYNCOBJ_DESTROY, &destroy) == 0,
		"SYNCOBJ_DESTROY");

	if (clone >= 0)
		delete_area(clone);
	mali_gem_close close = { create.handle, 0 };
	check(control(fd, MALI_IOCTL_GEM_CLOSE, &close) == 0, "GEM_CLOSE");
	check(control(fd, MALI_IOCTL_GEM_INFO, &info) != 0,
		"a closed handle is gone");
	close.handle = second.handle;
	control(fd, MALI_IOCTL_GEM_CLOSE, &close);

	// the address space is reused
	uint32 first = 0;
	uint32 highest = 0;
	bool all = true;
	for (int i = 0; i < 1000; i++) {
		mali_gem_create loop = { 1024 * 1024, 0, 0, 0 };
		if (control(fd, MALI_IOCTL_GEM_CREATE, &loop) != 0) {
			all = false;
			break;
		}
		mali_gem_info loopInfo = { loop.handle, 0, 0 };
		control(fd, MALI_IOCTL_GEM_INFO, &loopInfo);
		if (i == 0)
			first = loopInfo.va;
		if (loopInfo.va > highest)
			highest = loopInfo.va;
		mali_gem_close loopClose = { loop.handle, 0 };
		control(fd, MALI_IOCTL_GEM_CLOSE, &loopClose);
	}
	check(all && highest == first,
		"1000 buffers of 1 MiB made and closed at the same address");

	::close(fd);
	printf("%d failures\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
