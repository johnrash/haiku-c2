/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * The chips as tools/probes/weatherboard.cpp reads them (milestone M65): the
 * BME280 in forced mode with the datasheet's compensation formulas, the
 * Si1132 reset, given the datasheet's default UV coefficients and forced.
 */


#include "WeatherSensors.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <unistd.h>

#include <OS.h>


static const char* kBus = "header";
static const char* kLockPath = "/tmp/WeatherBoard.lock";

static const uint8 kClimateAddresses[] = { 0x76, 0x77 };
static const uint8 kClimateChipId = 0x60;
static const uint8 kLightAddress = 0x60;
static const uint8 kLightPartId = 0x32;

// BME280 registers
enum {
	BME_CALIB0 = 0x88,
	BME_CHIP_ID = 0xd0,
	BME_RESET = 0xe0,
	BME_CALIB1 = 0xe1,
	BME_CTRL_HUM = 0xf2,
	BME_STATUS = 0xf3,
	BME_CTRL_MEAS = 0xf4,
	BME_DATA = 0xf7
};

// Si1132 registers, commands and parameters
enum {
	SI_PART_ID = 0x00,
	SI_HW_KEY = 0x07,
	SI_UCOEF0 = 0x13,
	SI_PARAM_WR = 0x17,
	SI_COMMAND = 0x18,
	SI_RESPONSE = 0x20,
	SI_ALS_VIS_DATA0 = 0x22,
	SI_UVINDEX0 = 0x2c,
	SI_PARAM_RD = 0x2e,

	SI_CMD_NOP = 0x00,
	SI_CMD_RESET = 0x01,
	SI_CMD_ALS_FORCE = 0x06,
	SI_CMD_PARAM_SET = 0xa0,

	SI_PARAM_CHLIST = 0x01,
	SI_CHLIST_EN_UV = 0x80,
	SI_CHLIST_EN_ALS_IR = 0x20,
	SI_CHLIST_EN_ALS_VIS = 0x10
};


static uint16
le16(const uint8* data)
{
	return data[0] | (data[1] << 8);
}


/*!	flock() on the lock file for as long as it lives. */
class BusLock {
public:
	BusLock(int fd)
		:
		fFd(fd)
	{
		if (fFd >= 0) {
			while (flock(fFd, LOCK_EX) != 0 && errno == EINTR)
				;
		}
	}

	~BusLock()
	{
		if (fFd >= 0)
			flock(fFd, LOCK_UN);
	}

private:
	int fFd;
};


WeatherSensors::WeatherSensors()
	:
	fClimateOpen(false),
	fLightOpen(false),
	fLockFile(-1)
{
}


WeatherSensors::~WeatherSensors()
{
	Close();
}


status_t
WeatherSensors::Open()
{
	Close();

	fLockFile = open(kLockPath, O_RDWR | O_CREAT, 0666);
	status_t status;
	{
		BusLock lock(fLockFile);
		status = _OpenLocked();
	}
	if (status != B_OK)
		Close();
	return status;
}


status_t
WeatherSensors::_OpenLocked()
{
	for (size_t i = 0; i < sizeof(kClimateAddresses); i++) {
		uint8 chipId = 0;
		if (fClimate.Open(kBus, kClimateAddresses[i]) == B_OK
			&& fClimate.ReadRegister(BME_CHIP_ID, &chipId) == B_OK
			&& chipId == kClimateChipId) {
			fClimateOpen = true;
			break;
		}
		fClimate.Close();
	}
	if (fClimateOpen) {
		fClimate.WriteRegister(BME_RESET, 0xb6);
		snooze(5000);
		if (_ReadCalibration() != B_OK) {
			fClimate.Close();
			fClimateOpen = false;
		}
	}

	uint8 partId = 0;
	if (fLight.Open(kBus, kLightAddress) == B_OK
		&& fLight.ReadRegister(SI_PART_ID, &partId) == B_OK
		&& partId == kLightPartId && _InitLight() == B_OK) {
		fLightOpen = true;
	} else
		fLight.Close();

	return fClimateOpen || fLightOpen ? B_OK : B_DEVICE_NOT_FOUND;
}


void
WeatherSensors::Close()
{
	fClimate.Close();
	fLight.Close();
	fClimateOpen = false;
	fLightOpen = false;
	if (fLockFile >= 0) {
		close(fLockFile);
		fLockFile = -1;
	}
}


bool
WeatherSensors::IsOpen() const
{
	return fClimateOpen || fLightOpen;
}


status_t
WeatherSensors::Read(WeatherReading& reading)
{
	reading.climateValid = false;
	reading.lightValid = false;
	if (!IsOpen())
		return B_NO_INIT;

	BusLock lock(fLockFile);
	if (fClimateOpen)
		reading.climateValid = _MeasureClimate(reading) == B_OK;
	if (fLightOpen)
		reading.lightValid = _MeasureLight(reading) == B_OK;

	return reading.climateValid || reading.lightValid ? B_OK : B_IO_ERROR;
}


// #pragma mark - BME280


status_t
WeatherSensors::_ReadCalibration()
{
	uint8 reg = BME_CALIB0;
	uint8 c0[26];
	status_t status = fClimate.WriteRead(&reg, 1, c0, sizeof(c0));
	if (status != B_OK)
		return status;
	reg = BME_CALIB1;
	uint8 c1[7];
	status = fClimate.WriteRead(&reg, 1, c1, sizeof(c1));
	if (status != B_OK)
		return status;

	Calibration& cal = fCalibration;
	cal.t1 = le16(c0);
	cal.t2 = (int16)le16(c0 + 2);
	cal.t3 = (int16)le16(c0 + 4);
	cal.p1 = le16(c0 + 6);
	cal.p2 = (int16)le16(c0 + 8);
	cal.p3 = (int16)le16(c0 + 10);
	cal.p4 = (int16)le16(c0 + 12);
	cal.p5 = (int16)le16(c0 + 14);
	cal.p6 = (int16)le16(c0 + 16);
	cal.p7 = (int16)le16(c0 + 18);
	cal.p8 = (int16)le16(c0 + 20);
	cal.p9 = (int16)le16(c0 + 22);
	cal.h1 = c0[25];
	cal.h2 = (int16)le16(c1);
	cal.h3 = c1[2];
	// H4 and H5 are 12-bit signed values sharing 0xe5
	cal.h4 = (int16)(((int8)c1[3] << 4) | (c1[4] & 0x0f));
	cal.h5 = (int16)(((int8)c1[5] << 4) | (c1[4] >> 4));
	cal.h6 = (int8)c1[6];
	return cal.t1 != 0 && cal.p1 != 0 ? B_OK : B_BAD_DATA;
}


status_t
WeatherSensors::_MeasureClimate(WeatherReading& reading)
{
	// forced mode, 1x oversampling for all three; ctrl_hum takes effect
	// with the ctrl_meas write that follows it
	status_t status = fClimate.WriteRegister(BME_CTRL_HUM, 0x01);
	if (status == B_OK) {
		status = fClimate.WriteRegister(BME_CTRL_MEAS,
			(1 << 5) | (1 << 2) | 1);
	}
	if (status != B_OK)
		return status;

	bigtime_t start = system_time();
	uint8 value = 0;
	do {
		snooze(2000);
		status = fClimate.ReadRegister(BME_STATUS, &value);
		if (status != B_OK)
			return status;
	} while ((value & 0x08) != 0 && system_time() - start < 100000);
	if ((value & 0x08) != 0)
		return B_TIMED_OUT;

	uint8 reg = BME_DATA;
	uint8 data[8];
	status = fClimate.WriteRead(&reg, 1, data, sizeof(data));
	if (status != B_OK)
		return status;
	int32 adcP = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);
	int32 adcT = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
	int32 adcH = (data[6] << 8) | data[7];
	if (adcT == 0x80000 || adcP == 0x80000 || adcH == 0x8000)
		return B_BAD_DATA;

	const Calibration& cal = fCalibration;
	double var1 = (adcT / 16384.0 - cal.t1 / 1024.0) * cal.t2;
	double var2 = (adcT / 131072.0 - cal.t1 / 8192.0)
		* (adcT / 131072.0 - cal.t1 / 8192.0) * cal.t3;
	double tFine = var1 + var2;
	reading.temperature = tFine / 5120.0;

	var1 = tFine / 2.0 - 64000.0;
	var2 = var1 * var1 * cal.p6 / 32768.0;
	var2 = var2 + var1 * cal.p5 * 2.0;
	var2 = var2 / 4.0 + cal.p4 * 65536.0;
	var1 = (cal.p3 * var1 * var1 / 524288.0 + cal.p2 * var1) / 524288.0;
	var1 = (1.0 + var1 / 32768.0) * cal.p1;
	if (var1 == 0)
		return B_BAD_DATA;
	double p = 1048576.0 - adcP;
	p = (p - var2 / 4096.0) * 6250.0 / var1;
	var1 = cal.p9 * p * p / 2147483648.0;
	var2 = p * cal.p8 / 32768.0;
	reading.pressure = (p + (var1 + var2 + cal.p7) / 16.0) / 100.0;

	double h = tFine - 76800.0;
	h = (adcH - (cal.h4 * 64.0 + cal.h5 / 16384.0 * h))
		* (cal.h2 / 65536.0 * (1.0 + cal.h6 / 67108864.0 * h
			* (1.0 + cal.h3 / 67108864.0 * h)));
	h = h * (1.0 - cal.h1 * h / 524288.0);
	reading.humidity = h < 0 ? 0 : h > 100 ? 100 : h;
	return B_OK;
}


// #pragma mark - Si1132


status_t
WeatherSensors::_LightCommand(uint8 command)
{
	// clear the response counter, then wait for the command's answer
	status_t status = fLight.WriteRegister(SI_COMMAND, SI_CMD_NOP);
	if (status != B_OK)
		return status;
	uint8 response = 0xff;
	for (int i = 0; i < 50 && response != 0; i++) {
		status = fLight.ReadRegister(SI_RESPONSE, &response);
		if (status != B_OK)
			return status;
	}
	status = fLight.WriteRegister(SI_COMMAND, command);
	if (status != B_OK)
		return status;
	bigtime_t start = system_time();
	do {
		snooze(1000);
		status = fLight.ReadRegister(SI_RESPONSE, &response);
		if (status != B_OK)
			return status;
	} while (response == 0 && system_time() - start < 100000);
	if (response == 0)
		return B_TIMED_OUT;
	return (response & 0xf0) == 0x80 ? B_ERROR : B_OK;
}


status_t
WeatherSensors::_SetLightParameter(uint8 parameter, uint8 value)
{
	status_t status = fLight.WriteRegister(SI_PARAM_WR, value);
	if (status == B_OK)
		status = _LightCommand(SI_CMD_PARAM_SET | parameter);
	uint8 readBack = 0;
	if (status == B_OK)
		status = fLight.ReadRegister(SI_PARAM_RD, &readBack);
	if (status == B_OK && readBack != value)
		status = B_BAD_DATA;
	return status;
}


status_t
WeatherSensors::_InitLight()
{
	status_t status = fLight.WriteRegister(SI_COMMAND, SI_CMD_RESET);
	if (status != B_OK)
		return status;
	snooze(30000);
	status = fLight.WriteRegister(SI_HW_KEY, 0x17);
	if (status != B_OK)
		return status;

	// the datasheet's default UV coefficients
	static const uint8 kCoefficients[] = { 0x7b, 0x6b, 0x01, 0x00 };
	for (size_t i = 0; i < sizeof(kCoefficients); i++) {
		status = fLight.WriteRegister(SI_UCOEF0 + i, kCoefficients[i]);
		if (status != B_OK)
			return status;
	}
	return _SetLightParameter(SI_PARAM_CHLIST,
		SI_CHLIST_EN_UV | SI_CHLIST_EN_ALS_IR | SI_CHLIST_EN_ALS_VIS);
}


status_t
WeatherSensors::_MeasureLight(WeatherReading& reading)
{
	status_t status = _LightCommand(SI_CMD_ALS_FORCE);
	if (status != B_OK)
		return status;
	uint8 reg = SI_ALS_VIS_DATA0;
	uint8 data[4];
	status = fLight.WriteRead(&reg, 1, data, sizeof(data));
	if (status != B_OK)
		return status;
	reading.visible = le16(data);
	reading.infrared = le16(data + 2);
	reg = SI_UVINDEX0;
	status = fLight.WriteRead(&reg, 1, data, 2);
	if (status != B_OK)
		return status;
	reading.uvIndex = le16(data) / 100.0;
	return B_OK;
}
