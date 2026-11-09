# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
# tears.py <video> <start> <seconds> <crop w:h:x:y>: counts tears, pairs of
# consecutive 60 Hz captures where one changes part of the video area and
# the next changes exactly the rest (a new frame shown in two pieces)
import os, subprocess, sys, tempfile
import numpy as np
from PIL import Image
video, start, seconds, crop = sys.argv[1:5]
d = tempfile.mkdtemp()
w, h, x, y = crop.split(":")
subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-ss", start,
	"-t", seconds, "-i", video, "-vf",
	"crop=%s,scale=%d:%d" % (crop, int(w) // 2, int(h) // 2),
	os.path.join(d, "%04d.png")], check=True)
frames = [np.asarray(Image.open(os.path.join(d, f)).convert("L"),
	dtype=np.int16) for f in sorted(os.listdir(d))]
masks = []
for a, b in zip(frames, frames[1:]):
	changed = (np.abs(b - a).mean(axis=1) > 4).astype(float)
	masks.append(np.convolve(changed, np.ones(9) / 9, mode="same") > 0.5)
changes = sum(1 for m in masks if m.mean() > 0.05)
tears = []
for m1, m2 in zip(masks, masks[1:]):
	f1, f2 = m1.mean(), m2.mean()
	if 0.05 < f1 < 0.95 and 0.05 < f2 < 0.95 and (m1 & m2).mean() < 0.05 \
			and (m1 | m2).mean() > 0.9:
		e = np.where(np.diff(m1.astype(int)) != 0)[0]
		tears.append(int(e[0]) if len(e) else -1)
print("%s @%ss: %d captures with change, %d tears; tear rows (of %d): %s"
	% (video, start, changes, len(tears), int(h) // 2, tears[:20]))
