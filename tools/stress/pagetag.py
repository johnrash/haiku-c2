#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# Files whose every 4 KiB page says what it is, for finding where wrongly
# written data came from (docs/milestones.md, the eMMC corruption of 2026-11-01).
#
#   pagetag.py gen <file> <round> <file id> <bytes>
#       writes <bytes> bytes; page n starts with b"PGTG", round, file id,
#       page n (little-endian 32-bit each), then 4080 bytes derived from
#       those three numbers
#   pagetag.py verify <file>...
#       checks every page; for a wrong one says what it holds: another
#       page's tag (round/file/page), an "hpkg" package header, zeros, or
#       its first bytes. Exit status 1 if any page is wrong.
import hashlib
import os
import re
import struct
import sys

PAGE = 4096
MAGIC = b"PGTG"


def page_data(round_, file_id, page):
	head = MAGIC + struct.pack("<III", round_, file_id, page)
	seed = hashlib.sha256(head).digest()
	body = bytearray()
	block = seed
	while len(body) < PAGE - len(head):
		block = hashlib.sha256(block).digest()
		body += block
	return head + bytes(body[:PAGE - len(head)])


def gen(path, round_, file_id, size):
	with open(path, "wb") as f:
		page = 0
		written = 0
		while written < size:
			data = page_data(round_, file_id, page)
			piece = data[:min(PAGE, size - written)]
			f.write(piece)
			written += len(piece)
			page += 1


def describe(data):
	if data[:4] == MAGIC and len(data) >= 16:
		r, f, p = struct.unpack("<III", data[4:16])
		return "tag round %d file %d page %d" % (r, f, p)
	if data[:4] == b"hpkg":
		return "an hpkg header"
	if data == bytes(len(data)):
		return "zeros"
	return "bytes " + data[:16].hex()


def verify(path):
	bad = 0
	with open(path, "rb") as f:
		content = f.read()
	head = content[:16]
	if head[:4] != MAGIC:
		print("%s: first page not tagged (%s)" % (path, describe(content[:PAGE])))
		return 1
	r, fid, _ = struct.unpack("<III", head[4:16])
	# a file named as gen's callers name them (r<round>-f<file id>.dat)
	# must hold its own pages, not another tagged file's
	name = re.match(r"(?:bad-)?r(\d+)-f(\d+)\.dat$", os.path.basename(path))
	if name and (r, fid) != (int(name.group(1)), int(name.group(2))):
		print("%s: holds another file's pages (%s)" % (path,
			describe(content[:PAGE])))
		return 1
	pages = (len(content) + PAGE - 1) // PAGE
	for n in range(pages):
		got = content[n * PAGE:(n + 1) * PAGE]
		want = page_data(r, fid, n)[:len(got)]
		if got != want:
			bad += 1
			if bad <= 8:
				wrong = sum(1 for a, b in zip(got, want) if a != b)
				print("%s: page %d wrong (%d bytes): holds %s" % (path, n,
					wrong, describe(got)))
	if bad > 8:
		print("%s: ... %d wrong pages in all" % (path, bad))
	return 1 if bad else 0


def main():
	if len(sys.argv) >= 6 and sys.argv[1] == "gen":
		gen(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]))
		return 0
	if len(sys.argv) >= 3 and sys.argv[1] == "verify":
		status = 0
		for path in sys.argv[2:]:
			status |= verify(path)
		return status
	print(__doc__ if __doc__ else "usage: pagetag.py gen|verify ...")
	return 2


if __name__ == "__main__":
	sys.exit(main())
