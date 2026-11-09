#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
"""Sends one TCP SYN to an IP address the board doesn't have, in a frame
for its MAC address.

    sudo stray-syn.py <interface> <source ip> <dest ip> <dest mac> [<port>]

While an interface is being configured (IFF_CONFIGURING: net_server's DHCP
client waiting for a lease), Haiku's IPv4 receive path accepts any packet for
the interface's MAC address, so a listening socket spawns a child for the SYN
and the child's bind to the destination fails. Up to hrev60299 that child
kept the listener's local address and stayed queued, and closing the
listener panicked with "bound endpoint ... not in hash!".
"""
import socket
import struct
import sys


def checksum(data):
	if len(data) % 2:
		data += b"\0"
	total = sum(struct.unpack("!%dH" % (len(data) // 2), data))
	while total >> 16:
		total = (total & 0xffff) + (total >> 16)
	return ~total & 0xffff


def main():
	interface, source, destination, mac = sys.argv[1:5]
	port = int(sys.argv[5]) if len(sys.argv) > 5 else 22
	src = socket.inet_aton(source)
	dst = socket.inet_aton(destination)
	destinationMac = bytes.fromhex(mac.replace(":", ""))

	tcp = struct.pack("!HHIIBBHHH", 40000, port, 0x12345678, 0, 5 << 4,
		0x02, 65535, 0, 0)
	pseudo = src + dst + struct.pack("!BBH", 0, socket.IPPROTO_TCP, len(tcp))
	tcp = tcp[:16] + struct.pack("!H", checksum(pseudo + tcp)) + tcp[18:]

	ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(tcp), 0x4242, 0, 64,
		socket.IPPROTO_TCP, 0, src, dst)
	ip = ip[:10] + struct.pack("!H", checksum(ip)) + ip[12:]

	raw = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
	raw.bind((interface, 0))
	frame = destinationMac + raw.getsockname()[4] + struct.pack("!H", 0x0800) \
		+ ip + tcp
	raw.send(frame)
	print("sent SYN %s:40000 -> %s:%d (%s) on %s" % (source, destination, port,
		mac, interface))


if __name__ == "__main__":
	main()
