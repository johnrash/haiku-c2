#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# nv12-diff.py <frames.nv12> <reference.nv12> <width> <height>
# Compares decoded frames (NV12, no padding: mediadecode's nv12 output)
# with a reference decode of the same stream (FFmpeg's: ffmpeg -i <file>
# -f rawvideo -pix_fmt nv12 <reference>), frame by frame: the luma's PSNR,
# its largest difference and how many pixels differ by more than 2, and the
# first frame's first macroblock (16x16) on its own. MPEG-1/2 decoders may
# differ within the IDCT tolerance, so this measures rather than matches.
# Prints the frames that stand out and the totals; exit status 0 if the
# frame counts are the same and every frame's luma PSNR is 40 dB or more.
import sys

import numpy

frames_path, reference_path = sys.argv[1], sys.argv[2]
width, height = int(sys.argv[3]), int(sys.argv[4])
frame_size = width * height * 3 // 2
frames = numpy.fromfile(frames_path, dtype=numpy.uint8)
reference = numpy.fromfile(reference_path, dtype=numpy.uint8)
count = len(frames) // frame_size
reference_count = len(reference) // frame_size
print("%d frames decoded, %d in the reference" % (count, reference_count))

worst = None
bad = 0
for i in range(min(count, reference_count)):
	a = frames[i * frame_size:i * frame_size + width * height].reshape(height, width).astype(int)
	b = reference[i * frame_size:i * frame_size + width * height].reshape(height, width).astype(int)
	d = numpy.abs(a - b)
	mse = float((d * d).mean())
	psnr = 99.0 if mse == 0 else 10 * numpy.log10(255 * 255 / mse)
	if i == 0:
		block = d[:16, :16]
		print("frame 0, first macroblock: largest difference %d, mean %.2f"
			% (block.max(), block.mean()))
	if worst is None or psnr < worst[1]:
		worst = (i, psnr, int(d.max()), int((d > 2).sum()))
	if psnr < 40:
		bad += 1
		if bad <= 10:
			print("frame %d: luma PSNR %.1f dB, largest difference %d, %d pixels"
				" over 2" % (i, psnr, d.max(), (d > 2).sum()))
if worst is not None:
	print("worst: frame %d, luma PSNR %.1f dB, largest difference %d, %d pixels"
		" over 2" % worst)
print("%d frames under 40 dB" % bad)
sys.exit(0 if bad == 0 and count == reference_count else 1)
