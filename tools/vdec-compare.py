#!/usr/bin/env python3
# vdec-compare.py <frames.nv12> <stream.framemd5> [<width> <height>]
# Compares the frames vdectest wrote (NV12, no padding) with FFmpeg's
# per-frame checksums (tools/vdec-streams.sh): prints each frame that
# differs, then the totals. Exit status 0 if all frames match. Without a
# size, each frame is as large as its checksum line says (streams whose
# size changes; their checksums made part by part).
import hashlib
import sys

frames_path, md5_path = sys.argv[1], sys.argv[2]
frame_size = None
if len(sys.argv) > 4:
	frame_size = int(sys.argv[3]) * int(sys.argv[4]) * 3 // 2

expected = []
sizes = []
for line in open(md5_path):
	if line.startswith("#") or not line.strip():
		continue
	fields = [f.strip() for f in line.split(",")]
	if frame_size is not None and int(fields[4]) != frame_size:
		sys.exit("vdec-compare: FFmpeg's frames are %s bytes, not %d"
			% (fields[4], frame_size))
	expected.append(fields[5])
	sizes.append(int(fields[4]))

data = open(frames_path, "rb").read()
got = []
offset = 0
while True:
	size = frame_size if frame_size is not None \
		else sizes[min(len(got), len(sizes) - 1)]
	if offset + size > len(data):
		break
	got.append(hashlib.md5(data[offset:offset + size]).hexdigest())
	offset += size
if offset != len(data):
	print("%d bytes left over" % (len(data) - offset))

matching = 0
for i in range(max(len(got), len(expected))):
	a = got[i] if i < len(got) else None
	b = expected[i] if i < len(expected) else None
	if a == b:
		matching += 1
	else:
		where = b in got and got.index(b)
		print("frame %d: %s, FFmpeg %s%s" % (i, a, b,
			"" if where is False else " (decoder's frame %d)" % where))
print("%d of %d frames match (decoder %d, FFmpeg %d)" % (matching,
	len(expected), len(got), len(expected)))
sys.exit(0 if matching == len(expected) == len(got) else 1)
