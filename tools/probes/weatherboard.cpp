/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * Reads Hardkernel's Weather Board 2 on the header's I2C B (pins 27 and 28,
 * BI2CDevice's "header" bus; milestone M65 (docs/milestones.md)): a Bosch BME280 (temperature,
 * humidity, pressure) and a Silicon Labs Si1132 (UV index, visible and
 * infrared light).
 *
 *   weatherboard            scan the bus, identify both chips, one reading
 *   weatherboard <n> [s]    n readings, s seconds apart (default 2)
 *
 * The BME280 measures once per reading (forced mode, 1x oversampling) and
 * is compensated with the datasheet's floating point formulas from its
 * calibration words. The Si1132 is reset, given the datasheet's default UV
 * coefficients and forced to measure; visible and infrared light are the
 * converter's raw counts (its dark level is about 256), the UV index is the
 * chip's own (in hundredths).
 *
 * Exit status 0 if both chips answered with the right IDs and every reading
 * was in range.
 *
 * Build: $CXX -O2 -o weatherboard weatherboard.cpp -ldevice -lbe
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <I2CDevice.h>
#include <OS.h>


static const char* kBus = "header";

static const uint8 kBme280Addresses[] = { 0x76, 0x77 };
static const uint8 kBme280ChipId = 0x60;
static const uint8 kSi1132Address = 0x60;
static const uint8 kSi1132PartId = 0x32;

// BME280 registers
enum {
	BME_CALIB0 = 0x88,		// 26 bytes: T1..P9, (0xa0 unused), H1
	BME_CHIP_ID = 0xd0,
	BME_RESET = 0xe0,
	BME_CALIB1 = 0xe1,		// 7 bytes: H2..H6
	BME_CTRL_HUM = 0xf2,
	BME_STATUS = 0xf3,
	BME_CTRL_MEAS = 0xf4,
	BME_CONFIG = 0xf5,
	BME_DATA = 0xf7			// 8 bytes: pressure, temperature, humidity
};

// Si1132 registers, commands and parameters
enum {
	SI_PART_ID = 0x00,
	SI_REV_ID = 0x01,
	SI_SEQ_ID = 0x02,
	SI_HW_KEY = 0x07,
	SI_UCOEF0 = 0x13,
	SI_PARAM_WR = 0x17,
	SI_COMMAND = 0x18,
	SI_RESPONSE = 0x20,
	SI_ALS_VIS_DATA0 = 0x22,	// then IR (0x24), ..., UV index (0x2c)
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

struct bme280_calibration {
	uint16	t1;
	int16	t2, t3;
	uint16	p1;
	int16	p2, p3, p4, p5, p6, p7, p8, p9;
	uint8	h1, h3;
	int16	h2, h4, h5;
	int8	h6;
};

static int sFailures = 0;


static void
check(bool ok, const char* what)
{
	printf("%s: %s\n", ok ? "pass" : "FAIL", what);
	if (!ok)
		sFailures++;
}


static uint16
le16(const uint8* data)
{
	return data[0] | (data[1] << 8);
}


// #pragma mark - BME280


static status_t
bme280_read_calibration(BI2CDevice& device, bme280_calibration& calibration)
{
	uint8 reg = BME_CALIB0;
	uint8 c0[26];
	status_t status = device.WriteRead(&reg, 1, c0, sizeof(c0));
	if (status != B_OK)
		return status;
	reg = BME_CALIB1;
	uint8 c1[7];
	status = device.WriteRead(&reg, 1, c1, sizeof(c1));
	if (status != B_OK)
		return status;

	calibration.t1 = le16(c0);
	calibration.t2 = (int16)le16(c0 + 2);
	calibration.t3 = (int16)le16(c0 + 4);
	calibration.p1 = le16(c0 + 6);
	calibration.p2 = (int16)le16(c0 + 8);
	calibration.p3 = (int16)le16(c0 + 10);
	calibration.p4 = (int16)le16(c0 + 12);
	calibration.p5 = (int16)le16(c0 + 14);
	calibration.p6 = (int16)le16(c0 + 16);
	calibration.p7 = (int16)le16(c0 + 18);
	calibration.p8 = (int16)le16(c0 + 20);
	calibration.p9 = (int16)le16(c0 + 22);
	calibration.h1 = c0[25];
	calibration.h2 = (int16)le16(c1);
	calibration.h3 = c1[2];
	// H4 and H5 are 12-bit signed values sharing 0xe5
	calibration.h4 = (int16)(((int8)c1[3] << 4) | (c1[4] & 0x0f));
	calibration.h5 = (int16)(((int8)c1[5] << 4) | (c1[4] >> 4));
	calibration.h6 = (int8)c1[6];
	return B_OK;
}


static status_t
bme280_measure(BI2CDevice& device, const bme280_calibration& cal,
	double& temperature, double& pressure, double& humidity)
{
	// forced mode, 1x oversampling for all three; ctrl_hum takes effect
	// with the ctrl_meas write that follows it
	status_t status = device.WriteRegister(BME_CTRL_HUM, 0x01);
	if (status == B_OK)
		status = device.WriteRegister(BME_CTRL_MEAS, (1 << 5) | (1 << 2) | 1);
	if (status != B_OK)
		return status;

	// about 8 ms at 1x; wait for "measuring" to clear
	bigtime_t start = system_time();
	uint8 value = 0;
	do {
		snooze(2000);
		status = device.ReadRegister(BME_STATUS, &value);
		if (status != B_OK)
			return status;
	} while ((value & 0x08) != 0 && system_time() - start < 100000);
	if ((value & 0x08) != 0)
		return B_TIMED_OUT;

	uint8 reg = BME_DATA;
	uint8 data[8];
	status = device.WriteRead(&reg, 1, data, sizeof(data));
	if (status != B_OK)
		return status;
	int32 adcP = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);
	int32 adcT = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);
	int32 adcH = (data[6] << 8) | data[7];
	if (adcT == 0x80000 || adcP == 0x80000 || adcH == 0x8000)
		return B_BAD_DATA;
			// the values a skipped measurement reads as

	// the datasheet's compensation formulas
	double var1 = (adcT / 16384.0 - cal.t1 / 1024.0) * cal.t2;
	double var2 = (adcT / 131072.0 - cal.t1 / 8192.0)
		* (adcT / 131072.0 - cal.t1 / 8192.0) * cal.t3;
	double tFine = var1 + var2;
	temperature = tFine / 5120.0;

	var1 = tFine / 2.0 - 64000.0;
	var2 = var1 * var1 * cal.p6 / 32768.0;
	var2 = var2 + var1 * cal.p5 * 2.0;
	var2 = var2 / 4.0 + cal.p4 * 65536.0;
	var1 = (cal.p3 * var1 * var1 / 524288.0 + cal.p2 * var1) / 524288.0;
	var1 = (1.0 + var1 / 32768.0) * cal.p1;
	if (var1 == 0) {
		pressure = 0;
	} else {
		double p = 1048576.0 - adcP;
		p = (p - var2 / 4096.0) * 6250.0 / var1;
		var1 = cal.p9 * p * p / 2147483648.0;
		var2 = p * cal.p8 / 32768.0;
		pressure = (p + (var1 + var2 + cal.p7) / 16.0) / 100.0;
			// hPa
	}

	double h = tFine - 76800.0;
	h = (adcH - (cal.h4 * 64.0 + cal.h5 / 16384.0 * h))
		* (cal.h2 / 65536.0 * (1.0 + cal.h6 / 67108864.0 * h
			* (1.0 + cal.h3 / 67108864.0 * h)));
	h = h * (1.0 - cal.h1 * h / 524288.0);
	humidity = h < 0 ? 0 : h > 100 ? 100 : h;
	return B_OK;
}


// #pragma mark - Si1132


static status_t
si1132_command(BI2CDevice& device, uint8 command)
{
	// clear the response counter, then wait for the command's answer
	status_t status = device.WriteRegister(SI_COMMAND, SI_CMD_NOP);
	if (status != B_OK)
		return status;
	uint8 response = 0xff;
	for (int i = 0; i < 50 && response != 0; i++) {
		status = device.ReadRegister(SI_RESPONSE, &response);
		if (status != B_OK)
			return status;
	}
	status = device.WriteRegister(SI_COMMAND, command);
	if (status != B_OK)
		return status;
	bigtime_t start = system_time();
	do {
		snooze(1000);
		status = device.ReadRegister(SI_RESPONSE, &response);
		if (status != B_OK)
			return status;
	} while (response == 0 && system_time() - start < 100000);
	if (response == 0)
		return B_TIMED_OUT;
	if ((response & 0xf0) == 0x80) {
		fprintf(stderr, "si1132: command 0x%02x: error 0x%02x\n", command,
			response);
		return B_ERROR;
	}
	return B_OK;
}


static status_t
si1132_set_parameter(BI2CDevice& device, uint8 parameter, uint8 value)
{
	status_t status = device.WriteRegister(SI_PARAM_WR, value);
	if (status != B_OK)
		return status;
	status = si1132_command(device, SI_CMD_PARAM_SET | parameter);
	if (status != B_OK)
		return status;
	uint8 readBack;
	status = device.ReadRegister(SI_PARAM_RD, &readBack);
	if (status == B_OK && readBack != value)
		status = B_BAD_DATA;
	return status;
}


static status_t
si1132_init(BI2CDevice& device)
{
	status_t status = device.WriteRegister(SI_COMMAND, SI_CMD_RESET);
	if (status != B_OK)
		return status;
	snooze(30000);
	status = device.WriteRegister(SI_HW_KEY, 0x17);
	if (status != B_OK)
		return status;

	// the datasheet's default UV coefficients
	static const uint8 kCoefficients[] = { 0x7b, 0x6b, 0x01, 0x00 };
	for (size_t i = 0; i < sizeof(kCoefficients); i++) {
		status = device.WriteRegister(SI_UCOEF0 + i, kCoefficients[i]);
		if (status != B_OK)
			return status;
	}
	return si1132_set_parameter(device, SI_PARAM_CHLIST,
		SI_CHLIST_EN_UV | SI_CHLIST_EN_ALS_IR | SI_CHLIST_EN_ALS_VIS);
}


static status_t
si1132_measure(BI2CDevice& device, uint16& visible, uint16& infrared,
	double& uvIndex)
{
	status_t status = si1132_command(device, SI_CMD_ALS_FORCE);
	if (status != B_OK)
		return status;
	uint8 reg = SI_ALS_VIS_DATA0;
	uint8 data[4];
	status = device.WriteRead(&reg, 1, data, sizeof(data));
	if (status != B_OK)
		return status;
	visible = le16(data);
	infrared = le16(data + 2);
	reg = SI_UVINDEX0;
	status = device.WriteRead(&reg, 1, data, 2);
	if (status != B_OK)
		return status;
	uvIndex = le16(data) / 100.0;
	return B_OK;
}


// #pragma mark -


int
main(int argc, char** argv)
{
	int count = argc > 1 ? atoi(argv[1]) : 1;
	int seconds = argc > 2 ? atoi(argv[2]) : 2;
	if (count < 1)
		count = 1;

	BI2CDevice scanner;
	uint8 addresses[112];
	int32 found = 0;
	status_t status = scanner.Scan(kBus, addresses, 112, &found);
	check(status == B_OK, "scan of I2C B (pins 27 and 28)");
	printf("  answering:");
	for (int32 i = 0; i < found; i++)
		printf(" 0x%02x", addresses[i]);
	printf("%s\n", found == 0 ? " nothing" : "");

	// the BME280
	BI2CDevice bme;
	uint8 bmeAddress = 0;
	for (size_t i = 0; i < sizeof(kBme280Addresses); i++) {
		if (bme.Open(kBus, kBme280Addresses[i]) == B_OK && bme.IsPresent()) {
			bmeAddress = kBme280Addresses[i];
			break;
		}
		bme.Close();
	}
	uint8 chipId = 0;
	bool bmeOk = bmeAddress != 0
		&& bme.ReadRegister(BME_CHIP_ID, &chipId) == B_OK
		&& chipId == kBme280ChipId;
	char text[128];
	snprintf(text, sizeof(text), "BME280 at 0x%02x, chip ID 0x%02x",
		bmeAddress, chipId);
	check(bmeOk, text);
	bme280_calibration calibration = {};
	if (bmeOk) {
		bme.WriteRegister(BME_RESET, 0xb6);
		snooze(5000);
		bmeOk = bme280_read_calibration(bme, calibration) == B_OK;
		check(bmeOk && calibration.t1 != 0 && calibration.p1 != 0,
			"BME280 calibration words");
	}

	// the Si1132
	BI2CDevice si;
	uint8 partId = 0, revision = 0, sequencer = 0;
	bool siOk = si.Open(kBus, kSi1132Address) == B_OK
		&& si.ReadRegister(SI_PART_ID, &partId) == B_OK
		&& partId == kSi1132PartId;
	si.ReadRegister(SI_REV_ID, &revision);
	si.ReadRegister(SI_SEQ_ID, &sequencer);
	snprintf(text, sizeof(text), "Si1132 at 0x%02x, part ID 0x%02x "
		"(revision 0x%02x, sequencer 0x%02x)", kSi1132Address, partId,
		revision, sequencer);
	check(siOk, text);
	if (siOk) {
		siOk = si1132_init(si) == B_OK;
		check(siOk, "Si1132 reset, UV coefficients and channels set");
	}

	for (int i = 0; i < count; i++) {
		if (i > 0)
			sleep(seconds);
		if (bmeOk) {
			double t = 0, p = 0, h = 0;
			status = bme280_measure(bme, calibration, t, p, h);
			snprintf(text, sizeof(text), "BME280: %.2f C, %.1f %% "
				"humidity, %.2f hPa", t, h, p);
			check(status == B_OK && t > -40 && t < 85 && p > 300 && p < 1100
				&& h >= 0 && h <= 100, status == B_OK ? text
					: "BME280 measurement");
		}
		if (siOk) {
			uint16 visible = 0, infrared = 0;
			double uv = 0;
			status = si1132_measure(si, visible, infrared, uv);
			snprintf(text, sizeof(text), "Si1132: visible %u, infrared %u "
				"(raw counts), UV index %.2f", visible, infrared, uv);
			check(status == B_OK && uv < 20, status == B_OK ? text
				: "Si1132 measurement");
		}
	}

	printf("%d checks failed\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
