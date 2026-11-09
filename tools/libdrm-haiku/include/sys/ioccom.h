/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SYS_IOCCOM_H
#define _SYS_IOCCOM_H


/*!	ioctl request values in Linux's encoding, for Mesa's drm-uapi headers
	(include/drm-uapi/drm.h includes <sys/ioccom.h> on every system but
	Linux) and for meson_mali, which takes lima's requests unchanged
	(haiku/headers/private/drivers/meson_mali.h).

	Haiku's own <sys/ioccom.h> (headers/compatibility/bsd) has BSD's
	encoding, in which the "in" and "out" bits are the other way round:
	_IOWR() values agree, _IOR() and _IOW() values don't. So this directory
	has to come before Haiku's in the include path, and it has the same
	include guard.

	A request: bits 31-30 the direction (1: the caller passes data in, 2:
	the driver passes data back), 29-16 the size of the argument, 15-8 the
	group, 7-0 the number.
*/


#define _IOC_DIR_NONE	0UL
#define _IOC_DIR_IN		1UL
#define _IOC_DIR_OUT	2UL

#define _IOC(direction, group, number, size) \
	(((unsigned long)(direction) << 30) \
		| (((unsigned long)(size) & 0x3fff) << 16) \
		| (((unsigned long)(group) & 0xff) << 8) \
		| ((unsigned long)(number) & 0xff))

#define _IO(group, number) \
	_IOC(_IOC_DIR_NONE, (group), (number), 0)
#define _IOR(group, number, type) \
	_IOC(_IOC_DIR_OUT, (group), (number), sizeof(type))
#define _IOW(group, number, type) \
	_IOC(_IOC_DIR_IN, (group), (number), sizeof(type))
#define _IOWR(group, number, type) \
	_IOC(_IOC_DIR_IN | _IOC_DIR_OUT, (group), (number), sizeof(type))

// the size of a request's argument, the length Haiku's ioctl() takes
#define _IOC_SIZE(request)	(((unsigned long)(request) >> 16) & 0x3fff)


#endif	/* _SYS_IOCCOM_H */
