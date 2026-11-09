#!/usr/bin/env python3
"""Switch the ODROID-C2's DC power through the CH340 serial relay.

The relay sits in the 5V line wired COM->NC: released = board powered,
energized = board off. Usage: power.py on|off|cycle [off-seconds]
"""
import sys
import time

import serial

RELAY = "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
ENERGIZE = bytes([0xA0, 0x01, 0x01, 0xA2])	# board off
RELEASE = bytes([0xA0, 0x01, 0x00, 0xA1])	# board on


def send(command):
	with serial.Serial(RELAY, 9600, timeout=1) as port:
		port.write(command)
		port.flush()


def main():
	if len(sys.argv) < 2 or sys.argv[1] not in ("on", "off", "cycle"):
		sys.exit("usage: power.py on|off|cycle [off-seconds]")
	action = sys.argv[1]
	if action == "on":
		send(RELEASE)
	elif action == "off":
		send(ENERGIZE)
	else:
		send(ENERGIZE)
		time.sleep(float(sys.argv[2]) if len(sys.argv) > 2 else 2.0)
		send(RELEASE)
	print("power:", action)


if __name__ == "__main__":
	main()
