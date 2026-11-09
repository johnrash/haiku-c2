/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */


#include "Snapshot.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <NetworkAddress.h>
#include <NetworkInterface.h>
#include <NetworkRoster.h>


static const char* kScpiDevice = "/dev/power/meson_scpi/0";
static const bigtime_t kSlowInterval = 1000000;
static const bigtime_t kWeatherRetry = 5000000;


Sampler::Sampler()
	:
	fCpuInfos(NULL),
	fCpuCount(0),
	fLastActive(0),
	fLastTime(0),
	fLastSlow(0),
	fLastWeatherTry(0)
{
	system_info info;
	get_system_info(&info);
	fCpuCount = info.cpu_count;
	fCpuInfos = new cpu_info[fCpuCount];
	memset(&fSlow, 0, sizeof(fSlow));
	memset(fLastCoreActive, 0, sizeof(fLastCoreActive));
	fSlow.temperature = -1000;
}


Sampler::~Sampler()
{
	delete[] fCpuInfos;
}


void
Sampler::Sample(Snapshot& snapshot)
{
	bigtime_t now = system_time();
	if (now - fLastSlow >= kSlowInterval) {
		_SampleSlow(fSlow);
		fLastSlow = now;
	}
	snapshot = fSlow;

	// the load: active time of all cores, and of each, over the time since
	// the last call
	bigtime_t active = 0;
	bool valid = get_cpu_info(0, fCpuCount, fCpuInfos) == B_OK;
	if (valid) {
		for (uint32 i = 0; i < fCpuCount; i++)
			active += fCpuInfos[i].active_time;
	}
	snapshot.coreCount = min_c((int32)fCpuCount, kMaxCores);
	bool first = fLastTime == 0 || now <= fLastTime;
	for (int32 i = 0; i < snapshot.coreCount; i++) {
		float load = 0;
		if (valid && !first) {
			load = (float)(fCpuInfos[i].active_time - fLastCoreActive[i])
				/ (now - fLastTime);
		}
		snapshot.coreLoad[i] = load < 0 ? 0 : load > 1 ? 1 : load;
		if (valid)
			fLastCoreActive[i] = fCpuInfos[i].active_time;
	}
	if (!first) {
		float load = (float)(active - fLastActive)
			/ ((now - fLastTime) * (float)fCpuCount);
		snapshot.cpuLoad = load < 0 ? 0 : load > 1 ? 1 : load;
	} else
		snapshot.cpuLoad = 0;
	fLastActive = active;
	fLastTime = now;
}


void
Sampler::_SampleSlow(Snapshot& snapshot)
{
	system_info info;
	if (get_system_info(&info) == B_OK) {
		snapshot.memoryTotal = (uint64)info.max_pages * B_PAGE_SIZE;
		snapshot.memoryUsed = (uint64)info.used_pages * B_PAGE_SIZE;
		snapshot.teams = info.used_teams;
		snapshot.threads = info.used_threads;
	}
	snapshot.uptime = system_time();
	_ReadScpi(snapshot);
	_ReadAddress(snapshot);
	if (gethostname(snapshot.hostname, sizeof(snapshot.hostname)) != 0)
		strcpy(snapshot.hostname, "?");

	// the Weather Board, if one is on I2C B; looked for again every 5 s
	snapshot.weatherValid = false;
	bigtime_t now = system_time();
	if (!fWeather.IsOpen() && now - fLastWeatherTry >= kWeatherRetry) {
		fLastWeatherTry = now;
		fWeather.Open();
	}
	if (fWeather.IsOpen()) {
		if (fWeather.Read(snapshot.weather) == B_OK)
			snapshot.weatherValid = snapshot.weather.climateValid;
		else
			fWeather.Close();
	}
}


/*!	meson_scpi's text: "Current Temperature: 40.0 C", "CPU clock: 1536 MHz".
*/
void
Sampler::_ReadScpi(Snapshot& snapshot)
{
	snapshot.temperature = -1000;
	snapshot.clockMHz = 0;
	int fd = open(kScpiDevice, O_RDONLY);
	if (fd < 0)
		return;
	char text[512];
	ssize_t length = read(fd, text, sizeof(text) - 1);
	close(fd);
	if (length <= 0)
		return;
	text[length] = '\0';
	const char* temperature = strstr(text, "Current Temperature:");
	if (temperature != NULL)
		snapshot.temperature = strtof(temperature + 20, NULL);
	const char* clock = strstr(text, "CPU clock:");
	if (clock != NULL)
		snapshot.clockMHz = strtol(clock + 10, NULL, 10);
}


void
Sampler::_ReadAddress(Snapshot& snapshot)
{
	strcpy(snapshot.address, "no network");
	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;
	while (roster.GetNextInterface(&cookie, interface) == B_OK) {
		if ((interface.Flags() & IFF_LOOPBACK) != 0)
			continue;
		BNetworkInterfaceAddress address;
		for (int32 i = 0; interface.GetAddressAt(i, address) == B_OK; i++) {
			if (address.Address().Family() != AF_INET)
				continue;
			BString text = address.Address().ToString(false);
			if (text.Length() > 0) {
				strlcpy(snapshot.address, text.String(),
					sizeof(snapshot.address));
				return;
			}
		}
	}
}
