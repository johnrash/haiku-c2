/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef SNAPSHOT_H
#define SNAPSHOT_H


#include <OS.h>

#include "WeatherSensors.h"


static const int32 kMaxCores = 8;


/*!	What the demo's pages show, gathered by a thread of its own: some of it
	(the Weather Board, the SCPI's text) takes a few milliseconds to read.
*/
struct Snapshot {
	float			cpuLoad;			// 0 to 1, all cores
	int32			coreCount;
	float			coreLoad[kMaxCores];	// each core's, 0 to 1
	float			temperature;		// CPU, degrees Celsius; < -100: unknown
	int32			clockMHz;
	uint64			memoryUsed;			// bytes
	uint64			memoryTotal;
	int32			teams;
	int32			threads;
	char			address[32];		// the first IPv4 one that isn't loopback
	char			hostname[32];
	bigtime_t		uptime;
	bool			weatherValid;
	WeatherReading	weather;
};


class Sampler {
public:
								Sampler();
								~Sampler();

			// CPU load since the last call; the rest at most once a second
			void				Sample(Snapshot& snapshot);

private:
			void				_SampleSlow(Snapshot& snapshot);
			void				_ReadScpi(Snapshot& snapshot);
			void				_ReadAddress(Snapshot& snapshot);

			cpu_info*			fCpuInfos;
			uint32				fCpuCount;
			bigtime_t			fLastActive;
			bigtime_t			fLastCoreActive[kMaxCores];
			bigtime_t			fLastTime;
			bigtime_t			fLastSlow;
			WeatherSensors		fWeather;
			bigtime_t			fLastWeatherTry;
			Snapshot			fSlow;
};


#endif	// SNAPSHOT_H
