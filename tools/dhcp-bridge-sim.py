#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# dhcp-bridge-sim.py <bridge|normal> <seconds> [log]: a DHCP server on the
# crossover that leases the test board ($C2_BOARD_MAC) its usual 10.42.0.2
# for 60 s (renew at 30, rebind at 52), as a wireless bridge did.
# "bridge" also copies the bridge's quirks: a request sent to the server
# itself (a renewal) gets no answer, and a renewal or rebinding after the
# lease has ended gets a NAK. "normal" answers everything. Run with sudo,
# with dnsmasq stopped (both want port 67). Logs every message with the time
# since the last ACK.
import os
import socket
import struct
import sys
import time

DEVICE = os.environ.get("C2_HOST_IF", "eth1").encode()
SERVER = "10.42.0.1"
CLIENT = "10.42.0.2"
MAC = bytes.fromhex(os.environ.get("C2_BOARD_MAC", "00:1e:06:00:00:00")
	.replace(":", ""))
LEASE, RENEW, REBIND = 60, 30, 52
IP_PKTINFO = 8
NAMES = {1: "DISCOVER", 2: "OFFER", 3: "REQUEST", 4: "DECLINE", 5: "ACK",
	6: "NAK", 7: "RELEASE", 8: "INFORM"}

mode, duration = sys.argv[1], float(sys.argv[2])
log = open(sys.argv[3], "a") if len(sys.argv) > 3 else sys.stdout
last_ack = None
expiry = 0.0


def note(text):
	now = time.time()
	since = "%6.1f s after the ACK" % (now - last_ack) if last_ack else "no lease yet"
	log.write("%s (%s) %s\n" % (time.strftime("%H:%M:%S"), since, text))
	log.flush()


def options(data):
	result, i = {}, 0
	while i < len(data) and data[i] != 255:
		if data[i] == 0:
			i += 1
			continue
		result[data[i]] = data[i + 2:i + 2 + data[i + 1]]
		i += 2 + data[i + 1]
	return result


def reply(request, kind):
	ip = lambda a: socket.inet_aton(a)
	yiaddr = ip(CLIENT) if kind != 6 else bytes(4)
	header = struct.pack("!BBBB4sHH4s4s4s4s16s64s128s", 2, 1, 6, 0,
		request[4:8], 0, struct.unpack("!H", request[10:12])[0],
		request[12:16], yiaddr, bytes(4), request[24:28], request[28:44],
		b"", b"")
	opts = bytes([53, 1, kind]) + bytes([54, 4]) + ip(SERVER)
	if kind != 6:
		opts += bytes([51, 4]) + struct.pack("!I", LEASE)
		opts += bytes([58, 4]) + struct.pack("!I", RENEW)
		opts += bytes([59, 4]) + struct.pack("!I", REBIND)
		opts += bytes([1, 4]) + ip("255.255.255.0")
		opts += bytes([3, 4]) + ip(SERVER) + bytes([6, 4]) + ip(SERVER)
	return header + b"\x63\x82\x53\x63" + opts + b"\xff"


sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, DEVICE)
sock.setsockopt(socket.IPPROTO_IP, IP_PKTINFO, 1)
sock.bind(("", 67))
sock.settimeout(1)
note("started, mode %s, %d s" % (mode, duration))
end = time.time() + duration
while time.time() < end:
	try:
		data, ancillary, flags, source = sock.recvmsg(2048, socket.CMSG_SPACE(12))
	except socket.timeout:
		continue
	if len(data) < 240 or data[28:34] != MAC or data[0] != 1:
		continue
	destination = "?"
	for level, kind, value in ancillary:
		if level == socket.IPPROTO_IP and kind == IP_PKTINFO:
			destination = socket.inet_ntoa(value[8:12])
	opts = options(data[240:])
	kind = opts.get(53, b"\0")[0]
	ciaddr = socket.inet_ntoa(data[12:16])
	unicast = destination == SERVER
	note("%s from %s to %s, ciaddr %s%s" % (NAMES.get(kind, kind), source[0],
		destination, ciaddr, ", server id given" if 54 in opts else ""))
	answer = None
	if kind == 1:
		answer = 2
	elif kind == 3:
		renewal = ciaddr != "0.0.0.0" and 54 not in opts
		if mode == "bridge" and unicast:
			note("  ignored: sent to the server itself")
			continue
		if mode == "bridge" and renewal and time.time() > expiry:
			answer = 6
		else:
			answer = 5
	if answer is None:
		continue
	target = (ciaddr if unicast and ciaddr != "0.0.0.0" else "255.255.255.255", 68)
	sock.sendto(reply(data, answer), target)
	if answer == 5:
		expiry = time.time() + LEASE
		note("  %s to %s" % (NAMES[answer], target[0]))
		last_ack = time.time()
	else:
		note("  %s to %s" % (NAMES[answer], target[0]))
note("stopped")
