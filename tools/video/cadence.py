# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
# cadence.py <video> <start> <seconds> <crop>: intervals (in 60 Hz captures)
# between captures where the video area changed
import os, subprocess, sys, tempfile, collections
import numpy as np
from PIL import Image
video, start, seconds, crop = sys.argv[1:5]
d = tempfile.mkdtemp()
w, h, x, y = crop.split(":")
subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-ss", start,
	"-t", seconds, "-i", video, "-vf",
	"crop=%s,scale=%d:%d" % (crop, int(w) // 4, int(h) // 4),
	os.path.join(d, "%04d.png")], check=True)
frames = [np.asarray(Image.open(os.path.join(d, f)).convert("L"),
	dtype=np.int16) for f in sorted(os.listdir(d))]
changes = [i for i in range(1, len(frames))
	if np.abs(frames[i] - frames[i - 1]).mean() > 1.5]
gaps = [b - a for a, b in zip(changes, changes[1:])]
hist = collections.Counter(gaps)
print("%s @%s+%ss: %d changes (%.1f/s); gaps %s; longest %d captures (%d ms)"
	% (os.path.basename(video), start, seconds, len(changes),
	len(changes) / float(seconds), dict(sorted(hist.items())),
	max(gaps) if gaps else 0, (max(gaps) if gaps else 0) * 1000 // 60))
