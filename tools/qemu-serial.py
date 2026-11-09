#!/usr/bin/env python3
"""Type a line into a qemu-run.sh guest's serial port (e.g. KDL commands).
Output lands in the log file given to qemu-run.sh.
Usage: QEMU_NAME=<name> qemu-serial.py "<text>" """
import os
import socket
import sys
import time

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
s = socket.socket(socket.AF_UNIX)
s.connect(os.path.join(root, "run", os.environ.get("QEMU_NAME", "qemu") + "-serial.sock"))
s.sendall(sys.argv[1].encode() + b"\r")
time.sleep(0.5)
s.close()
