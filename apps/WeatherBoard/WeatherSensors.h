/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEATHER_SENSORS_H
#define WEATHER_SENSORS_H


#include <I2CDevice.h>


struct WeatherReading {
	bool	climateValid;		// the BME280's three
	double	temperature;		// degrees Celsius
	double	humidity;			// percent
	double	pressure;			// hPa

	bool	lightValid;			// the Si1132's three
	uint16	visible;			// raw counts (dark is about 256)
	uint16	infrared;
	double	uvIndex;
};


/*!	Hardkernel's Weather Board 2 on the header's I2C B (BI2CDevice's
	"header" bus, pins 27 and 28): a Bosch BME280 and a Silicon Labs Si1132.
	Every reading holds a lock file, so that several programs (the window
	and replicants) don't interleave their commands to the chips.
*/
class WeatherSensors {
public:
								WeatherSensors();
								~WeatherSensors();

			status_t			Open();
			void				Close();
			bool				IsOpen() const;

			status_t			Read(WeatherReading& reading);

private:
	struct Calibration {
		uint16	t1;
		int16	t2, t3;
		uint16	p1;
		int16	p2, p3, p4, p5, p6, p7, p8, p9;
		uint8	h1, h3;
		int16	h2, h4, h5;
		int8	h6;
	};

			status_t			_OpenLocked();
			status_t			_ReadCalibration();
			status_t			_MeasureClimate(WeatherReading& reading);
			status_t			_LightCommand(uint8 command);
			status_t			_SetLightParameter(uint8 parameter,
									uint8 value);
			status_t			_InitLight();
			status_t			_MeasureLight(WeatherReading& reading);

			BI2CDevice			fClimate;
			BI2CDevice			fLight;
			bool				fClimateOpen;
			bool				fLightOpen;
			Calibration			fCalibration;
			int					fLockFile;
};


#endif	// WEATHER_SENSORS_H
