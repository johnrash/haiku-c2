#!/usr/bin/env python3
"""Save a screenshot of the running qemu-run.sh guest as PNG.
Usage: qemu-shot.py <out.png>"""
import os
import socket
import subprocess
import sys
import time

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ppm = os.path.join(root, "run", "qemu-shot.ppm")
s = socket.socket(socket.AF_UNIX)
s.connect(os.path.join(root, "run", os.environ.get("QEMU_NAME", "qemu") + "-mon.sock"))
s.sendall(f"screendump {ppm}\n".encode())
time.sleep(1)
s.close()
subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", ppm,
	sys.argv[1]], check=True)
print("qemu-shot:", sys.argv[1])
