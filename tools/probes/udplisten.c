/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * udplisten <port> <seconds>: prints each UDP datagram that arrives on the
 * port (broadcast ones included), with its sender, for that long.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: udplisten <port> <seconds>\n");
		return 1;
	}
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	int on = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(atoi(argv[1]));
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, (struct sockaddr*)&address, sizeof(address)) != 0) {
		perror("bind");
		return 1;
	}
	printf("listening\n");
	fflush(stdout);
	time_t end = time(NULL) + atoi(argv[2]);
	while (time(NULL) < end) {
		fd_set set;
		FD_ZERO(&set);
		FD_SET(fd, &set);
		struct timeval timeout = { 1, 0 };
		if (select(fd + 1, &set, NULL, NULL, &timeout) <= 0)
			continue;
		char buffer[256];
		struct sockaddr_in from;
		socklen_t length = sizeof(from);
		ssize_t bytes = recvfrom(fd, buffer, sizeof(buffer) - 1, 0,
			(struct sockaddr*)&from, &length);
		if (bytes < 0)
			continue;
		buffer[bytes] = '\0';
		printf("%ld from %s: %s\n", (long)time(NULL), inet_ntoa(from.sin_addr),
			buffer);
		fflush(stdout);
	}
	return 0;
}
