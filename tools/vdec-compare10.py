#!/usr/bin/env python3
# vdec-compare10.py <frames.nv12> <reference.yuv420p10le> <width> <height>
# Compares the 8-bit NV12 frames the decoder wrote for a 10-bit stream
# (tools/probes/vdectest.cpp) with FFmpeg's 10-bit decoding
# (ffmpeg -f rawvideo -pix_fmt yuv420p10le), reduced to 8 bits by
# truncating and by rounding: per frame, which matches exactly, and the
# PSNR otherwise. Exit status 0 if every frame matches one of them.
import sys
import numpy as np

frames_path, ref_path = sys.argv[1], sys.argv[2]
w, h = int(sys.argv[3]), int(sys.argv[4])
got = np.fromfile(frames_path, np.uint8)
ref = np.fromfile(ref_path, np.uint16)
n_got = len(got) // (w * h * 3 // 2)
n_ref = len(ref) // (w * h * 3 // 2)
exact = {"truncated": 0, "rounded": 0}
worst = 99.0
for f in range(min(n_got, n_ref)):
	g = got[f * w * h * 3 // 2:(f + 1) * w * h * 3 // 2]
	r = ref[f * w * h * 3 // 2:(f + 1) * w * h * 3 // 2]
	ry = r[:w * h]
	ru = r[w * h:w * h + w * h // 4].reshape(h // 2, w // 2)
	rv = r[w * h + w * h // 4:].reshape(h // 2, w // 2)
	uv = np.empty((h // 2, w), np.uint16)
	uv[:, 0::2] = ru
	uv[:, 1::2] = rv
	r10 = np.concatenate([ry, uv.reshape(-1)])
	matched = None
	for name, r8 in (("truncated", r10 >> 2),
			("rounded", np.minimum((r10 + 2) >> 2, 255))):
		if np.array_equal(g, r8.astype(np.uint8)):
			exact[name] += 1
			matched = name
			break
	if matched is None:
		mse = np.mean((g.astype(float) - (r10 / 4.0)) ** 2)
		psnr = 10 * np.log10(255 * 255 / mse) if mse > 0 else 99.0
		worst = min(worst, psnr)
		if f < 5:
			print("frame %d: no exact match, PSNR %.2f dB" % (f, psnr))
print("%d frames (FFmpeg %d): %d match truncated, %d rounded; worst PSNR "
	"of the others %.2f dB" % (n_got, n_ref, exact["truncated"],
	exact["rounded"], worst))
sys.exit(0 if n_got == n_ref and sum(exact.values()) == n_got else 1)
