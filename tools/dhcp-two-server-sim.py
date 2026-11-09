#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# dhcp-two-server-sim.py <scenario> <seconds> [log]: two stand-in DHCP
# servers on the crossover, set up like a home network a C2 on Wi-Fi was on (docs/milestones.md
# M82). The "bridge" (10.42.0.1) offers at once and leases for 60 s; the
# "router" (10.42.0.254, sent from the laptop's hardware address) offers a
# second late and leases for a day, and answers requests that name the
# bridge, as the home router does. Both lease the test board ($C2_BOARD_MAC)
# its usual 10.42.0.2, so ssh keeps working; which lease the board took
# shows in its syslog (server, lease time). For a request naming the bridge:
#   nak       the router NAKs it after 100 ms, the bridge ACKs after 300 ms
#   ack       the router ACKs it (one day) after 100 ms, the bridge after 300
#   nak-noid  as nak, but the router's NAK has no server identifier
#   own-nak   the router stays silent, the bridge NAKs once, then ACKs
#   moved     the bridge is gone: the router NAKs 10.42.0.2 and offers
#             10.42.0.3, sent to that address unless the client set the
#             broadcast flag (as a BGW320 router did)
# The router answers two requests at most, so a client that takes its NAK
# still binds in the end. A request without a server identifier (rebooting,
# rebinding, renewing) gets the bridge's ACK alone. Every answer is a
# broadcast. Run with sudo, with dnsmasq stopped (both want port 67).
import os
import socket
import struct
import sys
import time

DEVICE = os.environ.get("C2_HOST_IF", "eth1")
BRIDGE, ROUTER = "10.42.0.1", "10.42.0.254"
CLIENT = "10.42.0.2"
MAC = bytes.fromhex(os.environ.get("C2_BOARD_MAC", "00:1e:06:00:00:00")
	.replace(":", ""))
LAPTOP_MAC = bytes.fromhex(open("/sys/class/net/%s/address" % DEVICE).read()
	.strip().replace(":", ""))
NAMES = {1: "DISCOVER", 2: "OFFER", 3: "REQUEST", 4: "DECLINE", 5: "ACK",
	6: "NAK", 7: "RELEASE", 8: "INFORM"}
LEASES = {BRIDGE: 60, ROUTER: 86400}
# (moved: the router's lease is a day as well)

scenario, duration = sys.argv[1], float(sys.argv[2])
if scenario not in ("nak", "ack", "nak-noid", "own-nak", "moved"):
	sys.exit("unknown scenario " + scenario)
log = open(sys.argv[3], "a") if len(sys.argv) > 3 else sys.stdout
start = time.time()


def note(text):
	log.write("%7.3f %s\n" % (time.time() - start, text))
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


def checksum(data):
	if len(data) % 2:
		data += b"\0"
	total = sum(struct.unpack("!%dH" % (len(data) // 2), data))
	while total >> 16:
		total = (total & 0xffff) + (total >> 16)
	return ~total & 0xffff


MOVED = "10.42.0.3"


def frame(server, payload, to=None):
	source = socket.inet_aton(server)
	destination = socket.inet_aton(to) if to else b"\xff" * 4
	udp = struct.pack("!HHHH", 67, 68, 8 + len(payload), 0) + payload
	pseudo = source + destination + struct.pack("!BBH", 0, 17, len(udp))
	udp = udp[:6] + struct.pack("!H", checksum(pseudo + udp) or 0xffff) \
		+ udp[8:]
	ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(udp), 0, 0, 64, 17,
		0, source, destination)
	ip = ip[:10] + struct.pack("!H", checksum(ip)) + ip[12:]
	return (MAC if to else b"\xff" * 6) + LAPTOP_MAC + b"\x08\x00" + ip + udp


def reply(request, server, kind, server_id=True, address=CLIENT):
	ip = socket.inet_aton
	yiaddr = ip(address) if kind != 6 else bytes(4)
	header = struct.pack("!BBBB4sHH4s4s4s4s16s64s128s", 2, 1, 6, 0,
		request[4:8], 0, 0x8000, request[12:16] if kind != 6 else bytes(4),
		yiaddr, bytes(4), request[24:28], request[28:44], b"", b"")
	opts = bytes([53, 1, kind])
	if server_id:
		opts += bytes([54, 4]) + ip(server)
	if kind != 6:
		lease = LEASES[server]
		opts += bytes([51, 4]) + struct.pack("!I", lease)
		opts += bytes([58, 4]) + struct.pack("!I", lease // 2)
		opts += bytes([59, 4]) + struct.pack("!I", lease * 7 // 8)
		opts += bytes([1, 4]) + ip("255.255.255.0")
		opts += bytes([3, 4]) + ip(BRIDGE) + bytes([6, 4]) + ip(BRIDGE)
	return header + b"\x63\x82\x53\x63" + opts + b"\xff"


receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
receiver.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
receiver.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE,
	DEVICE.encode())
receiver.bind(("", 67))
sender = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
sender.bind((DEVICE, 0))

pending = []
router_answers = 0
bridge_naks = 0


def later(delay, request, server, kind, server_id=True, address=CLIENT,
		to=None):
	pending.append((time.time() + delay, request, server, kind, server_id,
		address, to))


note("started, scenario %s, %d s" % (scenario, duration))
end = start + duration
while time.time() < end:
	now = time.time()
	for item in sorted(pending):
		if item[0] > now:
			break
		pending.remove(item)
		_, request, server, kind, server_id, address, to = item
		sender.send(frame(server, reply(request, server, kind, server_id,
			address), to))
		note("  %s %s%s%s" % ("bridge" if server == BRIDGE else "router",
			NAMES[kind], "" if server_id else " (no server identifier)",
			" to " + to if to else ""))
	wait = min([item[0] for item in pending] + [now + 0.2]) - now
	receiver.settimeout(max(wait, 0.001))
	try:
		data, source = receiver.recvfrom(2048)
	except socket.timeout:
		continue
	if len(data) < 240 or data[28:34] != MAC or data[0] != 1:
		continue
	opts = options(data[240:])
	kind = opts.get(53, b"\0")[0]
	named = socket.inet_ntoa(opts[54]) if 54 in opts else None
	broadcast = (struct.unpack("!H", data[10:12])[0] & 0x8000) != 0
	requested = socket.inet_ntoa(opts[50]) if 50 in opts \
		else socket.inet_ntoa(data[12:16])
	note("%s xid %s from %s%s%s%s" % (NAMES.get(kind, kind), data[4:8].hex(),
		source[0], ", names server " + named if named else "",
		", for " + requested if kind == 3 else "",
		", broadcast flag" if broadcast else ""))
	if scenario == "moved":
		to = None if broadcast else MOVED
		if kind == 1:
			later(0.1, data, ROUTER, 2, address=MOVED, to=to)
		elif kind == 3 and requested == MOVED and named in (None, ROUTER):
			later(0.1, data, ROUTER, 5, address=MOVED, to=to)
		elif kind == 3:
			later(0.1, data, ROUTER, 6)
		continue
	if kind == 1:
		later(0, data, BRIDGE, 2)
		later(1.0, data, ROUTER, 2)
	elif kind == 3 and named == BRIDGE:
		if scenario == "own-nak" and bridge_naks == 0:
			bridge_naks += 1
			later(0.1, data, BRIDGE, 6)
			continue
		if scenario != "own-nak" and router_answers < 2:
			router_answers += 1
			later(0.1, data, ROUTER, 5 if scenario == "ack" else 6,
				scenario != "nak-noid")
		later(0.3, data, BRIDGE, 5)
	elif kind == 3 and named is None:
		later(0.1, data, BRIDGE, 5)
note("stopped")
