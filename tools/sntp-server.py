#!/usr/bin/env python3
"""Minimal SNTP server for the C2's crossover link: answers NTP client
requests on 10.42.0.1:123 only, with the laptop's own clock.

Haiku sets its clock at boot from pool.ntp.org (the Time preferences'
default servers); dnsmasq on the link resolves those names to 10.42.0.1, so
the C2 gets the laptop's time without internet access. On another network
the same Haiku settings reach the real pool.

Needs root for port 123:  sudo setsid tools/sntp-server.py > run/sntp.log 2>&1 &
"""
import socket
import struct
import sys
import time

ADDRESS = "10.42.0.1"
NTP_EPOCH_OFFSET = 2208988800	# seconds from 1900 to 1970
IP_FREEBIND = 15				# bind before the link's address is up


def ntp_timestamp(t):
	seconds = int(t) + NTP_EPOCH_OFFSET
	fraction = int((t % 1) * (1 << 32)) & 0xffffffff
	return struct.pack("!II", seconds & 0xffffffff, fraction)


def main():
	sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	sock.setsockopt(socket.SOL_IP, IP_FREEBIND, 1)
	sock.bind((ADDRESS, 123))
	print(f"sntp-server: listening on {ADDRESS}:123", flush=True)
	while True:
		data, peer = sock.recvfrom(512)
		received = time.time()
		if len(data) < 48:
			continue
		version = (data[0] >> 3) & 7
		mode = data[0] & 7
		if mode != 3:	# client requests only
			continue
		now = time.time()
		reply = struct.pack("!BBbb", (0 << 6) | (version << 3) | 4, 3, data[2], -20)
		reply += struct.pack("!II", 0, 1 << 10)		# root delay, dispersion
		reply += b"LOCL"							# reference id
		reply += ntp_timestamp(now)					# reference timestamp
		reply += data[40:48]						# originate = client transmit
		reply += ntp_timestamp(received)			# receive timestamp
		reply += ntp_timestamp(time.time())			# transmit timestamp
		sock.sendto(reply, peer)
		print(f"sntp-server: {time.strftime('%F %T')} answered {peer[0]}", flush=True)


if __name__ == "__main__":
	sys.exit(main())
