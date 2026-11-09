#!/usr/bin/env python3
"""Run one QEMU monitor command against a qemu-run.sh instance.
Usage: QEMU_NAME=<name> qemu-mon.py "<command>" """
import os
import socket
import sys
import time

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = socket.socket(socket.AF_UNIX)
s.connect(os.path.join(root, "run", os.environ.get("QEMU_NAME", "qemu") + "-mon.sock"))
s.settimeout(1.0)
s.sendall((sys.argv[1] + "\n").encode())
data = b""
deadline = time.time() + 3
while time.time() < deadline:
	try:
		chunk = s.recv(65536)
		if not chunk:
			break
		data += chunk
	except socket.timeout:
		break
s.close()
text = data.decode("utf-8", "replace").replace("\r", "")
# drop the banner and the echoed command line
print("\n".join(l for l in text.split("\n") if not l.startswith("QEMU ") and "(qemu)" not in l))
