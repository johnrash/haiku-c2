/*
 * Copyright 2026, haiku-odroid-c2 port authors.
 * Distributed under the terms of the MIT License.
 *
 * TCP throughput from memory to memory, no disk: builds for Linux and
 * Haiku.
 *
 *   tcpblast recv <port> [<socket buffer KB>]
 *                                           accept one connection, read to
 *                                           its end, print MB/s; the socket's
 *                                           receive buffer (SO_RCVBUF) if given
 *   tcpblast send <host> <port> <MB> [<buffer KB> [<socket buffer KB>]]
 *                                           write that much, print MB/s;
 *                                           the socket's send buffer
 *                                           (SO_SNDBUF) if given
 *
 * Build: cc -O2 -o tcpblast tcpblast.c (Haiku: -lnetwork)
 */
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>


static double
now(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}


int
main(int argc, char** argv)
{
	if (argc >= 3 && strcmp(argv[1], "recv") == 0) {
		int listener = socket(AF_INET, SOCK_STREAM, 0);
		int one = 1;
		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		if (argc > 3) {
			int socketBuffer = atoi(argv[3]) * 1024;
			if (setsockopt(listener, SOL_SOCKET, SO_RCVBUF, &socketBuffer,
					sizeof(socketBuffer)) != 0)
				perror("tcpblast: SO_RCVBUF");
		}
		struct sockaddr_in address;
		memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(atoi(argv[2]));
		if (bind(listener, (struct sockaddr*)&address, sizeof(address)) != 0
			|| listen(listener, 1) != 0) {
			perror("tcpblast: bind/listen");
			return 1;
		}
		// a test starts its sender on this
		printf("listening\n");
		fflush(stdout);
		int fd = accept(listener, NULL, NULL);
		if (fd < 0) {
			perror("tcpblast: accept");
			return 1;
		}
		static char buffer[256 * 1024];
		double start = now();
		long long total = 0;
		ssize_t count;
		while ((count = read(fd, buffer, sizeof(buffer))) > 0)
			total += count;
		double seconds = now() - start;
		printf("received %.1f MB in %.2f s: %.1f MB/s\n", total / 1e6, seconds,
			total / 1e6 / seconds);
		close(fd);
		close(listener);
		return 0;
	}
	if (argc >= 5 && strcmp(argv[1], "send") == 0) {
		struct addrinfo hints, *info;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo(argv[2], argv[3], &hints, &info) != 0) {
			fprintf(stderr, "tcpblast: can't resolve %s\n", argv[2]);
			return 1;
		}
		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (argc > 6) {
			int socketBuffer = atoi(argv[6]) * 1024;
			if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &socketBuffer,
					sizeof(socketBuffer)) != 0) {
				perror("tcpblast: SO_SNDBUF");
			}
		}
		int actual = 0;
		socklen_t actualLength = sizeof(actual);
		getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &actual, &actualLength);
		fprintf(stderr, "send buffer %d KB\n", actual / 1024);
		if (connect(fd, info->ai_addr, info->ai_addrlen) != 0) {
			perror("tcpblast: connect");
			return 1;
		}
		long long total = atoll(argv[4]) * 1000000LL;
		size_t size = argc > 5 ? (size_t)atoi(argv[5]) * 1024 : 64 * 1024;
		char* buffer = calloc(1, size);
		double start = now();
		long long sent = 0;
		while (sent < total) {
			size_t chunk = total - sent < (long long)size
				? (size_t)(total - sent) : size;
			ssize_t count = write(fd, buffer, chunk);
			if (count <= 0) {
				perror("tcpblast: write");
				return 1;
			}
			sent += count;
		}
		shutdown(fd, SHUT_WR);
		char drain[64];
		while (read(fd, drain, sizeof(drain)) > 0)
			;
		double seconds = now() - start;
		printf("sent %.1f MB in %.2f s: %.1f MB/s\n", sent / 1e6, seconds,
			sent / 1e6 / seconds);
		close(fd);
		return 0;
	}
	fprintf(stderr, "usage: tcpblast recv <port>\n"
		"       tcpblast send <host> <port> <MB> [<buffer KB> "
		"[<socket buffer KB>]]\n");
	return 1;
}
