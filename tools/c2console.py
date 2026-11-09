#!/usr/bin/env python3
"""Own the C2 serial console: log everything, accept input from a FIFO.

Writes the exact byte stream to run/console.raw (used by c2wait) and a
timestamped text log to logs/<tag>-<date>.log. Bytes written to the FIFO
run/console.in are sent to the board (see c2send).

Usage: c2console.py <tag>      (runs in the foreground; start it with
                                setsid/nohup or a background shell)
"""
import datetime
import os
import select
import signal
import sys
import time

import serial

# the USB serial adapter on the C2's debug header (its /dev/serial/by-id path)
CONSOLE = os.environ.get("C2_CONSOLE",
	"/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_XXXXXXXX-if00-port0")
BAUD = 115200
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUN = os.path.join(ROOT, "run")
FIFO = os.path.join(RUN, "console.in")
RAW = os.path.join(RUN, "console.raw")
PIDFILE = os.path.join(RUN, "console.pid")


def stamp():
	return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


class TextLog:
	"""Timestamped line log; partial lines are flushed when the board idles,
	so prompts without a newline still show up."""

	def __init__(self, path):
		self.file = open(path, "a", buffering=1, encoding="utf-8")
		self.pending = ""

	def feed(self, data):
		# NUL bytes are line noise while the board is unpowered or unplugged
		self.pending += data.decode("utf-8", "replace").replace("\r", "").replace("\x00", "")
		while "\n" in self.pending:
			line, self.pending = self.pending.split("\n", 1)
			self.file.write(f"[{stamp()}] {line}\n")

	def flush_partial(self):
		if self.pending:
			self.file.write(f"[{stamp()}] {self.pending}\n")
			self.pending = ""

	def note(self, text):
		self.flush_partial()
		self.file.write(f"[{stamp()}] {text}\n")


def open_port(old):
	"""Open the console port, retrying until the adapter is present."""
	if old is not None:
		try:
			old.close()
		except Exception:
			pass
	while True:
		try:
			return serial.Serial(CONSOLE, BAUD, timeout=0)
		except (serial.SerialException, OSError):
			time.sleep(1)


def main():
	if len(sys.argv) != 2:
		sys.exit("usage: c2console.py <tag>   (e.g. m1)")
	os.makedirs(RUN, exist_ok=True)
	os.makedirs(os.path.join(ROOT, "logs"), exist_ok=True)
	if not os.path.exists(FIFO):
		os.mkfifo(FIFO)
	logPath = os.path.join(ROOT, "logs",
		f"{sys.argv[1]}-{datetime.date.today().isoformat()}.log")

	port = open_port(None)
	fifo = os.open(FIFO, os.O_RDWR | os.O_NONBLOCK)
	raw = open(RAW, "ab", buffering=0)
	log = TextLog(logPath)
	with open(PIDFILE, "w") as f:
		f.write(str(os.getpid()))
	log.note(f"=== c2console start, logging to {logPath}")

	def stop(*_):
		log.note("=== c2console stop")
		os.unlink(PIDFILE)
		sys.exit(0)
	signal.signal(signal.SIGTERM, stop)
	signal.signal(signal.SIGINT, stop)

	while True:
		try:
			ready, _, _ = select.select([port.fileno(), fifo], [], [], 0.3)
			if not ready:
				log.flush_partial()
				continue
			if port.fileno() in ready:
				data = port.read(4096)
				if data:
					raw.write(data)
					log.feed(data)
			if fifo in ready:
				data = os.read(fifo, 4096)
				if data:
					port.write(data)
					shown = data.decode("utf-8", "replace").strip()
					if shown:
						log.note(f">>> {shown}")
		except (serial.SerialException, OSError) as error:
			# USB adapter unplugged or hub power-cycled: wait for it to return
			log.note(f"=== serial port lost ({error}), waiting for it")
			port = open_port(port)
			log.note("=== serial port back")


if __name__ == "__main__":
	main()
