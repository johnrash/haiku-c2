#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
"""Stack traces from a Haiku arm64 core file, on this laptop.

    haiku-core.py <core> [<objects-dir>] [--thread <id>] [--frames <n>]

Reads the core's Haiku notes (team, images, threads with their registers,
dynamic symbols) and its memory, and walks each thread's frame pointer
chain. Addresses are shown as image + offset and the nearest dynamic
symbol; with the build's objects directory (generated.arm64/objects/haiku/
arm64/release), they also go through addr2line for file and line, when an
image of the same name is found there. Haiku's Debugger can't do this on
arm64 yet.
"""
import argparse
import os
import struct
import subprocess
import sys

NT_TEAM = 0x7465616d
NT_AREAS = 0x61726561
NT_IMAGES = 0x696d6167
NT_THREADS = 0x74687264
NT_SYMBOLS = 0x73796d73

THREAD_STATES = {1: "running", 2: "ready", 3: "receiving", 4: "asleep",
	5: "suspended", 6: "waiting"}
ADDR2LINE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(
	__file__))), "generated.arm64/cross-tools-arm64/bin/"
	"aarch64-unknown-haiku-addr2line")


class Core:
	def __init__(self, path):
		self.file = open(path, "rb")
		header = self.file.read(64)
		if header[:4] != b"\x7fELF" or header[4] != 2:
			raise SystemExit("not a 64-bit ELF core")
		phoff, = struct.unpack_from("<Q", header, 32)
		phentsize, phnum = struct.unpack_from("<HH", header, 54)
		self.segments = []
		self.images = []
		self.threads = []
		self.symbols = {}
		for i in range(phnum):
			self.file.seek(phoff + i * phentsize)
			kind, flags, offset, vaddr, paddr, filesz, memsz, align \
				= struct.unpack("<IIQQQQQQ", self.file.read(56))
			if kind == 1:
				self.segments.append((vaddr, filesz, offset))
			elif kind == 4:
				self.file.seek(offset)
				self._notes(self.file.read(filesz))

	def _notes(self, data):
		pos = 0
		while pos + 12 <= len(data):
			namesz, descsz, kind = struct.unpack_from("<III", data, pos)
			pos += 12
			name = data[pos:pos + namesz].rstrip(b"\0")
			pos += (namesz + 3) & ~3
			desc = data[pos:pos + descsz]
			pos += (descsz + 3) & ~3
			if name == b"Haiku":
				if kind == NT_IMAGES:
					self._images(desc)
				elif kind == NT_THREADS:
					self._threads(desc)
				elif kind == NT_SYMBOLS:
					self._symbols(desc)

	@staticmethod
	def _names(data, pos, count):
		names = []
		for _ in range(count):
			end = data.index(b"\0", pos)
			names.append(data[pos:end].decode(errors="replace"))
			pos = end + 1
		return names

	def _images(self, desc):
		count, size = struct.unpack_from("<II", desc, 0)
		entries = []
		for i in range(count):
			e = struct.unpack_from("<iiQQIiqQQqQQQQQ", desc, 8 + i * size)
			entries.append({"id": e[0], "text": e[7], "textSize": e[8],
				"delta": e[9]})
		for entry, name in zip(entries,
				self._names(desc, 8 + count * size, count)):
			entry["name"] = name
			self.images.append(entry)

	def _threads(self, desc):
		count, size, cpuSize = struct.unpack_from("<III", desc, 0)
		pos = 12
		entries = []
		for _ in range(count):
			tid, state, priority, _, stackBase, stackEnd \
				= struct.unpack_from("<iiiIQQ", desc, pos)
			regs = struct.unpack_from("<30QQQQ", desc, pos + size)
			entries.append({"id": tid, "state": state, "priority": priority,
				"stack": (stackBase, stackEnd), "x": regs[:30],
				"lr": regs[30], "sp": regs[31], "pc": regs[32]})
			pos += size + cpuSize
		for entry, name in zip(entries, self._names(desc, pos, count)):
			entry["name"] = name
			self.threads.append(entry)

	def _symbols(self, desc):
		imageId, count, size = struct.unpack_from("<iII", desc, 0)
		strings = 12 + count * size
		table = []
		for i in range(count):
			nameOffset, info, other, shndx, value, symSize \
				= struct.unpack_from("<IBBHQQ", desc, 12 + i * size)
			if value == 0 or info & 0xf not in (1, 2):	# object, function
				continue
			end = desc.index(b"\0", strings + nameOffset)
			table.append((value, symSize,
				desc[strings + nameOffset:end].decode(errors="replace")))
		self.symbols[imageId] = sorted(table)

	def read64(self, address):
		for base, size, offset in self.segments:
			if base <= address and address + 8 <= base + size:
				self.file.seek(offset + address - base)
				data = self.file.read(8)
				# a core cut off while it was written has holes
				return struct.unpack("<Q", data)[0] if len(data) == 8 else None
		return None

	def image_for(self, address):
		for image in self.images:
			if image["text"] <= address < image["text"] + image["textSize"]:
				return image
		return None

	def describe(self, address, objects):
		image = self.image_for(address)
		if image is None:
			return "%#x" % address
		offset = address - image["delta"]
		text = "%s+%#x" % (os.path.basename(image["name"]),
			address - image["text"])
		best = None
		for value, size, name in self.symbols.get(image["id"], []):
			if value <= address:
				best = (value, name)
			else:
				break
		if best is not None and address - best[0] < 0x100000:
			text += " %s+%#x" % (best[1], address - best[0])
		if objects is not None:
			line = _addr2line(objects, image["name"], offset)
			if line:
				text += "  " + line
		return text


_objectPaths = {}


def _addr2line(objects, imagePath, offset):
	name = os.path.basename(imagePath)
	if name not in _objectPaths:
		found = None
		for root, dirs, files in os.walk(objects):
			if name in files and "revisioned" not in root:
				found = os.path.join(root, name)
				break
		_objectPaths[name] = found
	path = _objectPaths[name]
	if path is None or not os.path.exists(ADDR2LINE):
		return None
	# the loaded image's text starts at its first segment's address
	out = subprocess.run([ADDR2LINE, "-f", "-C", "-i", "-e", path,
		"%#x" % offset], capture_output=True, text=True).stdout.split("\n")
	pairs = ["%s (%s)" % (out[i], out[i + 1].replace(
		os.path.dirname(os.path.dirname(ADDR2LINE)), "")) for i in
		range(0, len(out) - 1, 2) if out[i] != "??"]
	return " <- ".join(pairs)


def backtrace(core, thread, frames):
	addresses = [thread["pc"]]
	lr = thread["lr"]
	fp = thread["x"][29]
	if lr and core.image_for(lr):
		addresses.append(lr)
	low, high = thread["stack"]
	for _ in range(frames):
		if not low <= fp < high:
			break
		next_fp = core.read64(fp)
		ret = core.read64(fp + 8)
		if ret is None or next_fp is None:
			break
		if ret and (not addresses or ret != addresses[-1]):
			addresses.append(ret)
		if next_fp <= fp:
			break
		fp = next_fp
	return addresses


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("core")
	parser.add_argument("objects", nargs="?")
	parser.add_argument("--thread", type=int)
	parser.add_argument("--frames", type=int, default=40)
	args = parser.parse_args()
	core = Core(args.core)
	for thread in core.threads:
		if args.thread is not None and thread["id"] != args.thread:
			continue
		print("thread %d \"%s\" (%s, priority %d)" % (thread["id"],
			thread["name"], THREAD_STATES.get(thread["state"],
				thread["state"]), thread["priority"]))
		for i, address in enumerate(backtrace(core, thread, args.frames)):
			print("  %2d %#014x %s" % (i, address,
				core.describe(address, args.objects)))
		print()


if __name__ == "__main__":
	main()
