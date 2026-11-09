#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# arp-learning-test.py: how the test board's ARP learns from the network (docs/milestones.md
# M82). Sends hand-made ARP frames from the laptop's crossover interface,
# with made-up hardware addresses and the unused 10.42.0.77 and .99, and
# reads the board's table after each step. Run with sudo (raw socket); the
# board is reached over ssh. Checks: probes (sender 0.0.0.0) teach nothing
# and one for the board's own address is answered; a host that comes back
# with another hardware address is followed, not within a second of the
# last update unless it announces itself; the board's own entry can't be
# claimed; a failed request's rejection ends when the host talks, and
# otherwise after 20 s.
import os
import socket
import struct
import subprocess
import sys
import time

# the host's interface on the crossover cable, and the board's MAC
DEVICE = os.environ.get("C2_HOST_IF", "eth1")
BOARD_MAC_TEXT = os.environ.get("C2_BOARD_MAC", "00:1e:06:00:00:00").lower()
BOARD_IP, BOARD_MAC = "10.42.0.2", bytes.fromhex(BOARD_MAC_TEXT.replace(":", ""))
HOST_MAC_TEXT = open("/sys/class/net/%s/address" % DEVICE).read().strip()
LAPTOP_IP = "10.42.0.1"
BROADCAST = b"\xff" * 6
SSH = ["sshpass", "-p", "haiku", "ssh", "-o", "StrictHostKeyChecking=no",
	"-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=QUIET",
	"-o", "ConnectTimeout=8", "-o", "ControlMaster=auto",
	"-o", "ControlPath=/tmp/cm-arptest-%r@%h", "-o", "ControlPersist=120",
	"baron@" + BOARD_IP]


def mac(n):
	return bytes([2, 0, 0, 0, 0, n])


def arp_frame(eth_dst, eth_src, op, sha, spa, tha, tpa):
	return (eth_dst + eth_src + b"\x08\x06"
		+ struct.pack("!HHBBH", 1, 0x0800, 6, 4, op)
		+ sha + socket.inet_aton(spa) + tha + socket.inet_aton(tpa)
		+ bytes(18))


def board_entry(ip):
	out = subprocess.run(SSH + ["arp -a"], capture_output=True,
		text=True).stdout
	for line in out.splitlines():
		fields = line.split()
		if fields and fields[0] == ip:
			return " ".join(fields[1:])
	return "none"


def replies(sock, wait, from_mac):
	end = time.time() + wait
	got = 0
	while time.time() < end:
		sock.settimeout(max(0.01, end - time.time()))
		try:
			frame = sock.recv(1500)
		except socket.timeout:
			break
		if frame[12:14] == b"\x08\x06" and frame[6:12] == from_mac \
				and struct.unpack("!H", frame[20:22])[0] == 2:
			got += 1
	return got


results = []


def check(name, ok, detail):
	results.append(ok)
	print("%s %s: %s" % ("PASS" if ok else "FAIL", name, detail))
	sys.stdout.flush()


sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0806))
sock.bind((DEVICE, 0))
subprocess.run(SSH + ["arp -d 10.42.0.99; arp -d 10.42.0.77; arp -d 0.0.0.0"],
	capture_output=True)

# 1. probe for an unused address: no answer, nothing learned
sock.send(arp_frame(BROADCAST, mac(1), 1, mac(1), "0.0.0.0", bytes(6),
	"10.42.0.77"))
got = replies(sock, 1.0, BOARD_MAC)
zero = board_entry("0.0.0.0")
check("probe for an unused address", got == 0 and zero == "none",
	"%d replies, 0.0.0.0 entry: %s" % (got, zero))

# 2. probe for the board's own address: answered, nothing learned (from the
# laptop's own hardware address, or its card filters out the answer)
LAPTOP_MAC = bytes.fromhex(open("/sys/class/net/%s/address" % DEVICE).read()
	.strip().replace(":", ""))
sock.send(arp_frame(BROADCAST, LAPTOP_MAC, 1, LAPTOP_MAC, "0.0.0.0", bytes(6),
	BOARD_IP))
got = replies(sock, 1.0, BOARD_MAC)
zero = board_entry("0.0.0.0")
check("probe for the board's address", got >= 1 and zero == "none",
	"%d replies, 0.0.0.0 entry: %s" % (got, zero))

# 3. a host learned from its request, then the same address from another
# hardware address within a second (kept), then after it (followed)
sock.send(arp_frame(BROADCAST, mac(10), 1, mac(10), "10.42.0.99", bytes(6),
	LAPTOP_IP))
time.sleep(0.3)
sock.send(arp_frame(BROADCAST, mac(11), 1, mac(11), "10.42.0.99", bytes(6),
	LAPTOP_IP))
time.sleep(0.2)
entry = board_entry("10.42.0.99")
check("second address within a second", entry.startswith("02:00:00:00:00:0a"),
	entry)
time.sleep(1.5)
sock.send(arp_frame(BROADCAST, mac(11), 1, mac(11), "10.42.0.99", bytes(6),
	LAPTOP_IP))
time.sleep(0.2)
entry = board_entry("10.42.0.99")
check("moved host after a second", entry.startswith("02:00:00:00:00:0b"),
	entry)

# 4. its announcement (gratuitous) right after: followed at once
sock.send(arp_frame(BROADCAST, mac(12), 1, mac(12), "10.42.0.99", bytes(6),
	"10.42.0.99"))
time.sleep(0.2)
entry = board_entry("10.42.0.99")
check("announcement within a second", entry.startswith("02:00:00:00:00:0c"),
	entry)

# 5. a reply claiming the board's own address: its entry stays
time.sleep(1.5)
sock.send(arp_frame(BOARD_MAC, mac(13), 2, mac(13), BOARD_IP, BOARD_MAC,
	BOARD_IP))
time.sleep(0.2)
entry = board_entry(BOARD_IP)
check("claim on the board's address", entry.startswith(BOARD_MAC_TEXT)
	and "local" in entry, entry)

# 6. ordinary resolution still works
subprocess.run(SSH + ["arp -d " + LAPTOP_IP], capture_output=True)
ping = subprocess.run(SSH + ["ping -c 2 " + LAPTOP_IP], capture_output=True,
	text=True).stdout
entry = board_entry(LAPTOP_IP)
check("ordinary resolution", "2 packets received" in ping
	and entry.startswith(HOST_MAC_TEXT), entry)

# 7. a request that failed rejects the address; when the host then talks,
# it's followed (it used to stay rejected for as long as the host talked)
subprocess.run(SSH + ["arp -d 10.42.0.77; timeout 8 ping -c 1 10.42.0.77"],
	capture_output=True)
before = board_entry("10.42.0.77")
sock.send(arp_frame(BROADCAST, mac(20), 1, mac(20), "10.42.0.77", bytes(6),
	LAPTOP_IP))
time.sleep(0.3)
entry = board_entry("10.42.0.77")
check("failed address that talks", "reject" in before
	and entry.startswith("02:00:00:00:00:14") and "reject" not in entry,
	"before: %s, after: %s" % (before, entry))

# 8. a rejected address nobody hears from goes after 20 s, as before
subprocess.run(SSH + ["arp -d 10.42.0.78; timeout 8 ping -c 1 10.42.0.78"],
	capture_output=True)
before = board_entry("10.42.0.78")
time.sleep(22)
entry = board_entry("10.42.0.78")
check("silent rejected address expires", "reject" in before and entry == "none",
	"before: %s, 22 s later: %s" % (before, entry))

subprocess.run(SSH + ["arp -d 10.42.0.99; arp -d 10.42.0.77; arp -d 10.42.0.78"],
	capture_output=True)
subprocess.run(SSH + ["-O", "exit"], capture_output=True)
print("%d of %d passed" % (sum(results), len(results)))
sys.exit(0 if all(results) else 1)
