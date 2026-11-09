/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * halfclose <port>: accepts one connection, shuts its own sending side
 * down (shutdown(SHUT_WR)), then reads until the peer closes and prints the
 * byte count: data sent after a half-close must still arrive (milestone M82 (docs/milestones.md)).
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int
main(int argc, char** argv)
{
	int listener = socket(AF_INET, SOCK_STREAM, 0);
	int on = 1;
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(atoi(argv[1]));
	if (bind(listener, (struct sockaddr*)&address, sizeof(address)) != 0
		|| listen(listener, 1) != 0) {
		perror("halfclose");
		return 1;
	}
	printf("listening\n");
	fflush(stdout);
	int fd = accept(listener, NULL, NULL);
	shutdown(fd, SHUT_WR);
	static char buffer[65536];
	long long total = 0;
	ssize_t bytes;
	while ((bytes = recv(fd, buffer, sizeof(buffer), 0)) > 0)
		total += bytes;
	printf("received %lld bytes after the half-close, last recv %zd\n", total,
		bytes);
	close(fd);
	return 0;
}
