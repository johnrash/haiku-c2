/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * Tests of the meson_hdmi display driver's mode and power settings:
 *
 *   hdmitest dpms [on|standby|suspend|off [<seconds>]]
 *       shows the DPMS capabilities and state through app_server (BScreen);
 *       with a state, sets it, and after <seconds> (default 5) back to on
 *   hdmitest list
 *       the driver's mode list (from its shared info): index, size, refresh,
 *       clock, totals, sync polarity; the current and preferred ones marked
 *   hdmitest set <index> [<seconds>]
 *       sets that mode of the list through app_server (BScreen) and keeps it
 *       <seconds> (default 10); app_server goes back to its own mode when
 *       hdmitest quits
 *   hdmitest mode <width> <height> <total width> <total height> <kHz>
 *       sets a mode of the driver's list straight through the driver, past
 *       app_server. app_server keeps drawing as for its own mode; set that
 *       one again afterwards.
 *   hdmitest timing <kHz> <width> <sync start> <sync end> <total>
 *           <height> <sync start> <sync end> <total> [+h|-h] [+v|-v]
 *       the same with a complete timing, which the driver takes if the
 *       hardware can make it (for trying modes the display doesn't list);
 *       syncs negative unless given
 *
 * Build: $CXX -O2 $HAIKU_CPPFLAGS -I$HAIKU_SRC/headers/private/graphics/meson_hdmi \
 *            -o hdmitest hdmitest.cpp -lbe
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Application.h>
#include <OS.h>
#include <Screen.h>

#include "meson_hdmi_info.h"


static const char*
dpms_name(uint32 state)
{
	switch (state) {
		case B_DPMS_ON:
			return "on";
		case B_DPMS_STAND_BY:
			return "standby";
		case B_DPMS_SUSPEND:
			return "suspend";
		case B_DPMS_OFF:
			return "off";
	}
	return "?";
}


static int
dpms(int argc, char** argv)
{
	BApplication app("application/x-vnd.test-hdmitest");
	BScreen screen;
	uint32 capabilities = screen.DPMSCapabilites();
	printf("capabilities:%s%s%s%s; state %s\n",
		(capabilities & B_DPMS_ON) != 0 ? " on" : "",
		(capabilities & B_DPMS_STAND_BY) != 0 ? " standby" : "",
		(capabilities & B_DPMS_SUSPEND) != 0 ? " suspend" : "",
		(capabilities & B_DPMS_OFF) != 0 ? " off" : "",
		dpms_name(screen.DPMSState()));
	if (argc < 1)
		return 0;

	uint32 state = B_DPMS_ON;
	if (strcmp(argv[0], "standby") == 0)
		state = B_DPMS_STAND_BY;
	else if (strcmp(argv[0], "suspend") == 0)
		state = B_DPMS_SUSPEND;
	else if (strcmp(argv[0], "off") == 0)
		state = B_DPMS_OFF;
	status_t status = screen.SetDPMS(state);
	printf("set %s: %s; state %s\n", dpms_name(state), strerror(status),
		dpms_name(screen.DPMSState()));
	if (state == B_DPMS_ON)
		return status == B_OK ? 0 : 1;

	snooze((bigtime_t)((argc > 1 ? atof(argv[1]) : 5) * 1000000));
	status = screen.SetDPMS(B_DPMS_ON);
	printf("set on: %s; state %s\n", strerror(status),
		dpms_name(screen.DPMSState()));
	return status == B_OK ? 0 : 1;
}


static uint32
refresh_rate(const display_mode& mode)
{
	uint32 frame = (uint32)mode.timing.h_total * mode.timing.v_total;
	return frame == 0 ? 0
		: ((uint64)mode.timing.pixel_clock * 1000 + frame / 2) / frame;
}


static bool
same_mode(const display_mode& a, const display_mode& b)
{
	return memcmp(&a.timing, &b.timing, sizeof(display_timing)) == 0;
}


static void
print_mode(const char* prefix, const display_mode& mode)
{
	const display_timing& t = mode.timing;
	printf("%s%4ux%-4u @%3" B_PRIu32 " Hz %7" B_PRIu32 " kHz  h %u %u %u %u"
		"  v %u %u %u %u  %ch%cv", prefix, t.h_display, t.v_display,
		refresh_rate(mode), t.pixel_clock, t.h_display, t.h_sync_start,
		t.h_sync_end, t.h_total, t.v_display, t.v_sync_start, t.v_sync_end,
		t.v_total, (t.flags & B_POSITIVE_HSYNC) != 0 ? '+' : '-',
		(t.flags & B_POSITIVE_VSYNC) != 0 ? '+' : '-');
}


static int
list()
{
	int device = open("/dev/graphics/meson_hdmi/0", O_RDWR);
	if (device < 0) {
		perror("/dev/graphics/meson_hdmi/0");
		return 1;
	}
	area_id sharedArea;
	if (ioctl(device, MESON_HDMI_GET_SHARED_INFO, &sharedArea,
			sizeof(sharedArea)) != 0) {
		perror("MESON_HDMI_GET_SHARED_INFO");
		return 1;
	}
	meson_hdmi_shared_info* shared;
	area_id clone = clone_area("hdmitest shared info", (void**)&shared,
		B_ANY_ADDRESS, B_READ_AREA, sharedArea);
	if (clone < 0) {
		fprintf(stderr, "clone_area: %s\n", strerror(clone));
		return 1;
	}
	printf("%" B_PRIu32 " modes, EDID %" B_PRIu32 " bytes\n",
		shared->mode_count, shared->edid_size);
	for (uint32 i = 0; i < shared->mode_count; i++) {
		char prefix[16];
		snprintf(prefix, sizeof(prefix), "%2" B_PRIu32 " ", i);
		print_mode(prefix, shared->modes[i]);
		printf("%s%s\n", same_mode(shared->modes[i], shared->current_mode)
				? "  current" : "",
			i == shared->preferred_mode ? "  preferred" : "");
	}
	print_mode("current: ", shared->current_mode);
	printf("\n");
	delete_area(clone);
	close(device);
	return 0;
}


static int
set(int argc, char** argv)
{
	if (argc < 1) {
		fprintf(stderr, "set <index> [<seconds>]\n");
		return 1;
	}
	BApplication app("application/x-vnd.test-hdmitest");
	BScreen screen;
	display_mode* modes;
	uint32 count;
	if (screen.GetModeList(&modes, &count) != B_OK) {
		fprintf(stderr, "no mode list\n");
		return 1;
	}
	uint32 index = atoi(argv[0]);
	if (index >= count) {
		fprintf(stderr, "only %" B_PRIu32 " modes\n", count);
		return 1;
	}
	display_mode old;
	screen.GetMode(&old);
	bigtime_t start = system_time();
	status_t status = screen.SetMode(&modes[index], false);
	bigtime_t took = system_time() - start;
	print_mode("set ", modes[index]);
	printf(": %s (%" B_PRIdBIGTIME " ms)\n", strerror(status), took / 1000);
	display_mode now;
	screen.GetMode(&now);
	print_mode("now ", now);
	printf("\n");
	fflush(stdout);
	if (status == B_OK)
		snooze((bigtime_t)((argc > 1 ? atof(argv[1]) : 10) * 1000000));
	print_mode("back to ", old);
	printf("\n");
	free(modes);
	return status == B_OK ? 0 : 1;
}


static int
set_through_driver(display_mode& displayMode)
{
	int device = open("/dev/graphics/meson_hdmi/0", O_RDWR);
	if (device < 0) {
		perror("/dev/graphics/meson_hdmi/0");
		return 1;
	}
	displayMode.virtual_width = displayMode.timing.h_display;
	displayMode.virtual_height = displayMode.timing.v_display;
	displayMode.space = B_RGB32;
	if (ioctl(device, MESON_HDMI_SET_DISPLAY_MODE, &displayMode,
			sizeof(displayMode)) != 0) {
		printf("set mode: %s\n", strerror(errno));
		close(device);
		return 1;
	}
	print_mode("set ", displayMode);
	printf("\n");
	close(device);
	return 0;
}


static int
timing(int argc, char** argv)
{
	if (argc < 9) {
		fprintf(stderr, "timing <kHz> <width> <sync start> <sync end> <total> "
			"<height> <sync start> <sync end> <total> [+h|-h] [+v|-v]\n");
		return 1;
	}
	display_mode displayMode;
	memset(&displayMode, 0, sizeof(displayMode));
	display_timing& t = displayMode.timing;
	t.pixel_clock = atoi(argv[0]);
	t.h_display = atoi(argv[1]);
	t.h_sync_start = atoi(argv[2]);
	t.h_sync_end = atoi(argv[3]);
	t.h_total = atoi(argv[4]);
	t.v_display = atoi(argv[5]);
	t.v_sync_start = atoi(argv[6]);
	t.v_sync_end = atoi(argv[7]);
	t.v_total = atoi(argv[8]);
	for (int i = 9; i < argc; i++) {
		if (strcmp(argv[i], "+h") == 0)
			t.flags |= B_POSITIVE_HSYNC;
		else if (strcmp(argv[i], "+v") == 0)
			t.flags |= B_POSITIVE_VSYNC;
	}
	return set_through_driver(displayMode);
}


static int
mode(int argc, char** argv)
{
	if (argc < 5) {
		fprintf(stderr, "mode <width> <height> <total width> <total height> "
			"<kHz>\n");
		return 1;
	}
	display_mode displayMode;
	memset(&displayMode, 0, sizeof(displayMode));
	displayMode.timing.h_display = atoi(argv[0]);
	displayMode.timing.v_display = atoi(argv[1]);
	displayMode.timing.h_total = atoi(argv[2]);
	displayMode.timing.v_total = atoi(argv[3]);
	displayMode.timing.pixel_clock = atoi(argv[4]);
	return set_through_driver(displayMode);
}


int
main(int argc, char** argv)
{
	if (argc > 1 && strcmp(argv[1], "dpms") == 0)
		return dpms(argc - 2, argv + 2);
	if (argc > 1 && strcmp(argv[1], "list") == 0)
		return list();
	if (argc > 1 && strcmp(argv[1], "set") == 0)
		return set(argc - 2, argv + 2);
	if (argc > 1 && strcmp(argv[1], "mode") == 0)
		return mode(argc - 2, argv + 2);
	if (argc > 1 && strcmp(argv[1], "timing") == 0)
		return timing(argc - 2, argv + 2);
	fprintf(stderr, "usage: %s dpms [on|standby|suspend|off [<seconds>]]\n"
		"       %s list\n"
		"       %s set <index> [<seconds>]\n"
		"       %s mode <width> <height> <total width> <total height> "
		"<kHz>\n"
		"       %s timing <kHz> <width> <sync start> <sync end> <total> "
		"<height> <sync start> <sync end> <total> [+h|-h] [+v|-v]\n",
		argv[0], argv[0], argv[0], argv[0], argv[0]);
	return 1;
}
