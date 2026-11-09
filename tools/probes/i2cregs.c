/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * Reads or writes registers of an I2C device through the I2C bus manager's
 * raw bus device (the "i2c" tool only scans):
 *
 *   i2cregs <bus device> <address> <register> <count>         read
 *   i2cregs <bus device> <address> <register> -w <byte>...    write
 *   i2cregs <bus device> <address> <register> <count> -r <n>  read n times,
 *       printing the time each read took
 *
 * e.g. i2cregs /dev/bus/i2c/0/bus_raw 0x51 0 16 (the RTC shield's PCF8563)
 *
 * Build: $CC -O2 -I$HAIKU_SRC/headers/private/i2c -o i2cregs i2cregs.c
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Drivers.h>
#include <OS.h>

#include <i2c.h>


int
main(int argc, char** argv)
{
	if (argc < 5) {
		fprintf(stderr, "usage: %s <bus device> <address> <register> "
			"(<count> [-r <n>] | -w <byte>...)\n", argv[0]);
		return 1;
	}
	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "%s: %s\n", argv[1], strerror(errno));
		return 1;
	}
	uint8 reg = strtoul(argv[3], NULL, 0);
	i2c_ioctl_exec exec;
	memset(&exec, 0, sizeof(exec));
	exec.addr = strtoul(argv[2], NULL, 0);
	exec.cmdBuffer = &reg;
	exec.cmdLength = 1;

	uint8 buffer[I2C_EXEC_MAX_BUFFER_LENGTH];
	if (strcmp(argv[4], "-w") == 0) {
		size_t count = 0;
		for (int i = 5; i < argc && count < sizeof(buffer); i++)
			buffer[count++] = strtoul(argv[i], NULL, 0);
		exec.op = I2C_OP_WRITE_STOP;
		exec.buffer = buffer;
		exec.bufferLength = count;
		if (ioctl(fd, I2CEXEC, &exec, sizeof(exec)) != 0) {
			fprintf(stderr, "write failed: %s\n", strerror(errno));
			return 1;
		}
		return 0;
	}

	size_t count = strtoul(argv[4], NULL, 0);
	if (count > sizeof(buffer))
		count = sizeof(buffer);
	int repeat = 1;
	if (argc > 6 && strcmp(argv[5], "-r") == 0)
		repeat = atoi(argv[6]);
	for (int n = 0; n < repeat; n++) {
		exec.op = I2C_OP_READ_STOP;
		exec.buffer = buffer;
		exec.bufferLength = count;
		bigtime_t start = system_time();
		int result = ioctl(fd, I2CEXEC, &exec, sizeof(exec));
		bigtime_t took = system_time() - start;
		if (result != 0) {
			printf("read failed: %s (%lld us)\n", strerror(errno),
				(long long)took);
			continue;
		}
		for (size_t i = 0; i < count; i++)
			printf("%02x ", buffer[i]);
		if (repeat > 1)
			printf(" (%lld us)", (long long)took);
		printf("\n");
	}
	close(fd);
	return 0;
}
