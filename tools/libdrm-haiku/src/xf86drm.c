/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */

/*!	The libdrm functions Mesa's lima driver calls, for Haiku's meson_mali
	driver (docs/m43-mesa-lima-haiku.md §1.1, §1.4, §1.7):
	- drmIoctl() passes the request through, with the argument's size, as
	  Haiku's ioctl() wants it;
	- meson_mali has no DRM_IOCTL_VERSION: drmGetVersion() answers "lima"
	  1.1, so that lima makes growable heap buffers for its tile heaps;
	- sync files are emulated: an exported one is a pipe that is readable
	  at once, made after the sync object was signalled; importing one does
	  nothing;
	- no device enumeration, dma-bufs or device lookup by name;
	- buffers are mapped by cloning the area whose id DRM_IOCTL_LIMA_GEM_INFO
	  returns as the "offset" (drmHaikuMap()): Haiku can't mmap() a device.

	With LIMA_HAIKU_NOOP set (and not "0"), no ioctl reaches a driver: a
	stand-in answers as a Mali-450 with 3 PP cores whose jobs do nothing,
	so that the whole stack runs without the kernel driver (design §4,
	stage 0). Its buffers are anonymous areas of the team.
*/


#include <xf86drm.h>

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>

#include <drm-uapi/lima_drm.h>
#include <meson_mali.h>


// The requests must be the ones meson_mali takes: that holds as long as
// this directory's <sys/ioccom.h> is the one Mesa's drm.h gets.
_Static_assert(DRM_IOCTL_GEM_CLOSE == MALI_IOCTL_GEM_CLOSE, "GEM_CLOSE");
_Static_assert(DRM_IOCTL_SYNCOBJ_CREATE == MALI_IOCTL_SYNCOBJ_CREATE,
	"SYNCOBJ_CREATE");
_Static_assert(DRM_IOCTL_SYNCOBJ_DESTROY == MALI_IOCTL_SYNCOBJ_DESTROY,
	"SYNCOBJ_DESTROY");
_Static_assert(DRM_IOCTL_SYNCOBJ_WAIT == MALI_IOCTL_SYNCOBJ_WAIT,
	"SYNCOBJ_WAIT");
_Static_assert(DRM_IOCTL_LIMA_GET_PARAM == MALI_IOCTL_GET_PARAM, "GET_PARAM");
_Static_assert(DRM_IOCTL_LIMA_GEM_CREATE == MALI_IOCTL_GEM_CREATE,
	"GEM_CREATE");
_Static_assert(DRM_IOCTL_LIMA_GEM_INFO == MALI_IOCTL_GEM_INFO, "GEM_INFO");
_Static_assert(DRM_IOCTL_LIMA_GEM_SUBMIT == MALI_IOCTL_GEM_SUBMIT,
	"GEM_SUBMIT");
_Static_assert(DRM_IOCTL_LIMA_GEM_WAIT == MALI_IOCTL_GEM_WAIT, "GEM_WAIT");
_Static_assert(DRM_IOCTL_LIMA_CTX_CREATE == MALI_IOCTL_CTX_CREATE,
	"CTX_CREATE");
_Static_assert(DRM_IOCTL_LIMA_CTX_FREE == MALI_IOCTL_CTX_FREE, "CTX_FREE");
_Static_assert(sizeof(struct drm_lima_gem_submit) == sizeof(mali_gem_submit),
	"gem_submit");
_Static_assert(sizeof(struct drm_lima_m450_pp_frame)
	== sizeof(mali_m450_pp_frame), "m450_pp_frame");
_Static_assert(sizeof(struct drm_lima_gp_frame) == sizeof(mali_gp_frame),
	"gp_frame");
_Static_assert(sizeof(struct drm_syncobj_wait) == sizeof(mali_syncobj_wait),
	"syncobj_wait");


//	#pragma mark - the no-op stand-in


typedef struct noop_buffer {
	area_id		area;		// 0: a free slot
	uint32_t	address;	// on the "GPU"
} noop_buffer;

static const uint32_t kNoopFirstAddress = 0x10000000;
static const uint32_t kNoopLastAddress = 0xf0000000;

static pthread_mutex_t sNoopLock = PTHREAD_MUTEX_INITIALIZER;
static noop_buffer* sNoopBuffers;
static uint32_t sNoopBufferSlots;
static uint32_t sNoopNextAddress = 0x10000000;
static uint32_t sNoopNextSyncobj = 1;
static uint32_t sNoopNextContext = 1;
static int sNoop = -1;


static bool
noop_enabled(void)
{
	if (sNoop < 0) {
		const char* value = getenv("LIMA_HAIKU_NOOP");
		sNoop = value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
	}
	return sNoop != 0;
}


static int
noop_error(int error)
{
	errno = error;
	return -1;
}


static int
noop_gem_create(struct drm_lima_gem_create* create)
{
	if (create->size == 0 || create->pad != 0
		|| (create->flags & ~LIMA_BO_FLAG_HEAP) != 0) {
		return noop_error(EINVAL);
	}
	size_t size = (create->size + B_PAGE_SIZE - 1) & ~(size_t)(B_PAGE_SIZE - 1);
	void* address;
	area_id area = create_area("lima noop buffer", &address, B_ANY_ADDRESS,
		size, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	if (area < 0)
		return noop_error(area);

	pthread_mutex_lock(&sNoopLock);
	uint32_t slot = 0;
	while (slot < sNoopBufferSlots && sNoopBuffers[slot].area != 0)
		slot++;
	if (slot == sNoopBufferSlots) {
		uint32_t slots = sNoopBufferSlots > 0 ? sNoopBufferSlots * 2 : 64;
		noop_buffer* buffers = realloc(sNoopBuffers,
			slots * sizeof(noop_buffer));
		if (buffers == NULL) {
			pthread_mutex_unlock(&sNoopLock);
			delete_area(area);
			return noop_error(ENOMEM);
		}
		memset(buffers + sNoopBufferSlots, 0,
			(slots - sNoopBufferSlots) * sizeof(noop_buffer));
		sNoopBuffers = buffers;
		sNoopBufferSlots = slots;
	}
	// addresses are only written into other buffers, never used: they may
	// start again from the bottom
	if ((uint64_t)sNoopNextAddress + size > kNoopLastAddress)
		sNoopNextAddress = kNoopFirstAddress;
	sNoopBuffers[slot].area = area;
	sNoopBuffers[slot].address = sNoopNextAddress;
	sNoopNextAddress += size;
	pthread_mutex_unlock(&sNoopLock);

	create->handle = slot + 1;
	create->size = size;
	return 0;
}


/*!	The slot of \a handle, with sNoopLock held, or NULL. */
static noop_buffer*
noop_look_up(uint32_t handle)
{
	if (handle == 0 || handle > sNoopBufferSlots
		|| sNoopBuffers[handle - 1].area == 0) {
		return NULL;
	}
	return &sNoopBuffers[handle - 1];
}


static int
noop_ioctl(unsigned long request, void* arg)
{
	switch (request) {
		case DRM_IOCTL_LIMA_GET_PARAM:
		{
			struct drm_lima_get_param* param = arg;
			switch (param->param) {
				case DRM_LIMA_PARAM_GPU_ID:
					param->value = DRM_LIMA_PARAM_GPU_ID_MALI450;
					return 0;
				case DRM_LIMA_PARAM_NUM_PP:
					param->value = 3;
					return 0;
				case DRM_LIMA_PARAM_GP_VERSION:
				case DRM_LIMA_PARAM_PP_VERSION:
					param->value = 0;
					return 0;
			}
			return noop_error(EINVAL);
		}

		case DRM_IOCTL_LIMA_GEM_CREATE:
			return noop_gem_create(arg);

		case DRM_IOCTL_LIMA_GEM_INFO:
		{
			struct drm_lima_gem_info* info = arg;
			pthread_mutex_lock(&sNoopLock);
			noop_buffer* buffer = noop_look_up(info->handle);
			if (buffer != NULL) {
				info->va = buffer->address;
				info->offset = (uint64_t)buffer->area;
			}
			pthread_mutex_unlock(&sNoopLock);
			return buffer != NULL ? 0 : noop_error(ENOENT);
		}

		case DRM_IOCTL_GEM_CLOSE:
		{
			struct drm_gem_close* close = arg;
			area_id area = -1;
			pthread_mutex_lock(&sNoopLock);
			noop_buffer* buffer = noop_look_up(close->handle);
			if (buffer != NULL) {
				area = buffer->area;
				buffer->area = 0;
			}
			pthread_mutex_unlock(&sNoopLock);
			if (area < 0)
				return noop_error(EINVAL);
			// clones (drmHaikuMap()) keep the memory until they go
			delete_area(area);
			return 0;
		}

		case DRM_IOCTL_LIMA_CTX_CREATE:
		{
			struct drm_lima_ctx_create* create = arg;
			pthread_mutex_lock(&sNoopLock);
			create->id = sNoopNextContext++;
			pthread_mutex_unlock(&sNoopLock);
			return 0;
		}

		case DRM_IOCTL_SYNCOBJ_CREATE:
		{
			struct drm_syncobj_create* create = arg;
			pthread_mutex_lock(&sNoopLock);
			create->handle = sNoopNextSyncobj++;
			pthread_mutex_unlock(&sNoopLock);
			return 0;
		}

		// jobs finish at once
		case DRM_IOCTL_LIMA_GEM_SUBMIT:
		case DRM_IOCTL_LIMA_GEM_WAIT:
		case DRM_IOCTL_LIMA_CTX_FREE:
		case DRM_IOCTL_SYNCOBJ_DESTROY:
		case DRM_IOCTL_SYNCOBJ_WAIT:
			return 0;
	}
	return noop_error(EINVAL);
}


//	#pragma mark - libdrm


int
drmIoctl(int fd, unsigned long request, void* arg)
{
	if (noop_enabled())
		return noop_ioctl(request, arg);

	int result;
	do {
		result = ioctl(fd, request, arg, _IOC_SIZE(request));
	} while (result == -1 && (errno == EINTR || errno == EAGAIN));
	return result;
}


drmVersionPtr
drmGetVersion(int fd)
{
	(void)fd;

	drmVersionPtr version = calloc(1, sizeof(drmVersion));
	if (version == NULL)
		return NULL;
	version->version_major = 1;
	version->version_minor = 1;
	version->version_patchlevel = 0;
	version->name = strdup("lima");
	version->date = strdup("20260101");
	version->desc = strdup("Haiku meson_mali (Mali-450)");
	if (version->name == NULL || version->date == NULL
		|| version->desc == NULL) {
		drmFreeVersion(version);
		return NULL;
	}
	version->name_len = strlen(version->name);
	version->date_len = strlen(version->date);
	version->desc_len = strlen(version->desc);
	return version;
}


void
drmFreeVersion(drmVersionPtr version)
{
	if (version == NULL)
		return;
	free(version->name);
	free(version->date);
	free(version->desc);
	free(version);
}


int
drmGetDevice2(int fd, uint32_t flags, drmDevicePtr* device)
{
	(void)fd;
	(void)flags;
	if (device != NULL)
		*device = NULL;
	return ENODEV;
}


void
drmFreeDevice(drmDevicePtr* device)
{
	if (device == NULL)
		return;
	free(*device);
	*device = NULL;
}


int
drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int* primeFD)
{
	(void)fd;
	(void)handle;
	(void)flags;
	(void)primeFD;
	errno = EINVAL;
	return -1;
}


int
drmPrimeFDToHandle(int fd, int primeFD, uint32_t* handle)
{
	(void)fd;
	(void)primeFD;
	(void)handle;
	errno = EINVAL;
	return -1;
}


int
drmSyncobjCreate(int fd, uint32_t flags, uint32_t* handle)
{
	struct drm_syncobj_create create = { .handle = 0, .flags = flags };
	int result = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &create);
	if (result != 0)
		return result;
	*handle = create.handle;
	return 0;
}


int
drmSyncobjDestroy(int fd, uint32_t handle)
{
	struct drm_syncobj_destroy destroy = { .handle = handle, .pad = 0 };
	return drmIoctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &destroy);
}


int
drmSyncobjWait(int fd, uint32_t* handles, unsigned handleCount,
	int64_t timeoutNsec, unsigned flags, uint32_t* firstSignaled)
{
	struct drm_syncobj_wait wait = {
		.handles = (uint64_t)(uintptr_t)handles,
		.timeout_nsec = timeoutNsec,
		.count_handles = handleCount,
		.flags = flags,
	};
	if (drmIoctl(fd, DRM_IOCTL_SYNCOBJ_WAIT, &wait) != 0)
		return errno;
	if (firstSignaled != NULL)
		*firstSignaled = wait.first_signaled;
	return 0;
}


int
drmSyncobjExportSyncFile(int fd, uint32_t handle, int* syncFileFD)
{
	// INT64_MAX: forever
	int status = drmSyncobjWait(fd, &handle, 1, INT64_MAX, 0, NULL);
	if (status != 0) {
		errno = status;
		return -1;
	}

	int fds[2];
	if (pipe(fds) != 0)
		return -1;
	const char signalled = 1;
	if (write(fds[1], &signalled, 1) != 1) {
		int error = errno;
		close(fds[0]);
		close(fds[1]);
		errno = error;
		return -1;
	}
	close(fds[1]);
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	*syncFileFD = fds[0];
	return 0;
}


int
drmSyncobjImportSyncFile(int fd, uint32_t handle, int syncFileFD)
{
	(void)fd;
	(void)handle;
	(void)syncFileFD;
	return 0;
}


int
drmOpenWithType(const char* name, const char* busID, int type)
{
	(void)name;
	(void)busID;
	(void)type;
	errno = ENODEV;
	return -1;
}


//	#pragma mark - Haiku


void*
drmHaikuMap(int fd, uint64_t offset, size_t size)
{
	if (offset == 0 || offset > INT32_MAX) {
		errno = EINVAL;
		return NULL;
	}

	void* address = NULL;
	area_id area = clone_area("lima buffer", &address, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, (area_id)offset);
	if (area < 0) {
		errno = area;
		return NULL;
	}
	area_info info;
	if (get_area_info(area, &info) != B_OK || info.size < size) {
		delete_area(area);
		errno = EINVAL;
		return NULL;
	}
	// The driver keeps the CPU's caches in step only for the buffers it is
	// told are mapped (from the first time it is told): a mapping it
	// doesn't know of mustn't be used. A driver that doesn't know the
	// request keeps every buffer in step.
	if (!noop_enabled()) {
		int32 mapped = (int32)offset;
		if (ioctl(fd, MALI_IOCTL_HAIKU_CPU_MAPPED, &mapped, sizeof(mapped))
				!= 0 && errno != B_DEV_INVALID_IOCTL) {
			int error = errno;
			delete_area(area);
			errno = error;
			return NULL;
		}
	}
	return address;
}


int
drmHaikuUnmap(void* address, size_t size)
{
	(void)size;
	area_id area = area_for(address);
	if (area < 0) {
		errno = area;
		return -1;
	}
	status_t status = delete_area(area);
	if (status != B_OK) {
		errno = status;
		return -1;
	}
	return 0;
}
