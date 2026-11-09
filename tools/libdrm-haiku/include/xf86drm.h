/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef _XF86DRM_H_
#define _XF86DRM_H_


/*!	A stand-in for libdrm's xf86drm.h on Haiku: the part of libdrm's
	interface that Mesa's lima driver (and the Gallium code built with it)
	uses, for the meson_mali driver (docs/m43-mesa-lima-haiku.md §1.1, §1.7,
	§3.5). The structures and ioctl numbers are those of Mesa's own
	include/drm-uapi headers; this directory's <sys/ioccom.h> gives them
	Linux's encoding.

	Return values follow libdrm: the functions that wrap an ioctl return -1
	and set errno; drmGetDevice2() and drmSyncobjWait() return a negative
	error code (Haiku's error codes are negative already).
*/


#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>

#include <drm-uapi/drm.h>


#ifdef __cplusplus
extern "C" {
#endif


#define DRM_CLOEXEC			O_CLOEXEC
#define DRM_RDWR			O_RDWR

// device node types (drmOpenWithType())
#define DRM_NODE_PRIMARY	0
#define DRM_NODE_CONTROL	1
#define DRM_NODE_RENDER		2
#define DRM_NODE_MAX		3

// drmDevice::bustype
#define DRM_BUS_PCI			0
#define DRM_BUS_USB			1
#define DRM_BUS_PLATFORM	2
#define DRM_BUS_HOST1X		3

typedef struct _drmVersion {
	int		version_major;
	int		version_minor;
	int		version_patchlevel;
	int		name_len;
	char*	name;
	int		date_len;
	char*	date;
	int		desc_len;
	char*	desc;
} drmVersion, *drmVersionPtr;

typedef struct _drmPlatformBusInfo {
	char	fullname[512];
} drmPlatformBusInfo, *drmPlatformBusInfoPtr;

typedef struct _drmPlatformDeviceInfo {
	char**	compatible;		// NULL terminated
} drmPlatformDeviceInfo, *drmPlatformDeviceInfoPtr;

typedef struct _drmDevice {
	char**	nodes;
	int		available_nodes;
	int		bustype;
	union {
		drmPlatformBusInfoPtr		platform;
	} businfo;
	union {
		drmPlatformDeviceInfoPtr	platform;
	} deviceinfo;
} drmDevice, *drmDevicePtr;


// the request goes to the driver as is, with its argument's size; retried
// while it is interrupted
extern int drmIoctl(int fd, unsigned long request, void* arg);

// "lima" 1.0, made up here: meson_mali has no DRM_IOCTL_VERSION
extern drmVersionPtr drmGetVersion(int fd);
extern void drmFreeVersion(drmVersionPtr version);

// no device enumeration: always fails
extern int drmGetDevice2(int fd, uint32_t flags, drmDevicePtr* device);
extern void drmFreeDevice(drmDevicePtr* device);

// no dma-bufs: always fail
extern int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags,
	int* primeFD);
extern int drmPrimeFDToHandle(int fd, int primeFD, uint32_t* handle);

extern int drmSyncobjCreate(int fd, uint32_t flags, uint32_t* handle);
extern int drmSyncobjDestroy(int fd, uint32_t handle);
extern int drmSyncobjWait(int fd, uint32_t* handles, unsigned handleCount,
	int64_t timeoutNsec, unsigned flags, uint32_t* firstSignaled);

// Sync files, emulated: export waits until the sync object is signalled,
// then returns a pipe that is readable at once; import does nothing (every
// exported file is signalled already).
extern int drmSyncobjExportSyncFile(int fd, uint32_t handle, int* syncFileFD);
extern int drmSyncobjImportSyncFile(int fd, uint32_t handle, int syncFileFD);

// no device lookup by name: always fails
extern int drmOpenWithType(const char* name, const char* busID, int type);

// Haiku: maps a buffer by cloning the area whose id is its "offset" (what
// DRM_IOCTL_LIMA_GEM_INFO returns), as mmap() of the DRM file does on
// Linux. NULL on failure.
extern void* drmHaikuMap(int fd, uint64_t offset, size_t size);
extern int drmHaikuUnmap(void* address, size_t size);


#ifdef __cplusplus
}
#endif


#endif	/* _XF86DRM_H_ */
