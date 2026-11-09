# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
# bitsread.py <capture.mkv> <start> <seconds> <video w:h:x:y>: the frame
# number each 60 Hz capture shows, from the clip's 11 bit boxes
# (the clip: tools/video/README). <video w:h:x:y> is where the video is on
# screen, e.g. 1712:963:103:51 for a maximized MediaPlayer window.
import subprocess, sys, collections
import numpy as np
cap, start, seconds, rect = sys.argv[1:5]
vw, vh, vx, vy = (int(v) for v in rect.split(":"))
k = vw / 1920.0
raw = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-ss",
	start, "-t", seconds, "-i", cap, "-f", "rawvideo", "-pix_fmt", "gray", "-"],
	check=True, stdout=subprocess.PIPE).stdout
F = np.frombuffer(raw, np.uint8).reshape(-1, 1080, 1920)
ids = []
for f in F:
	n = 0
	for b in range(11):
		cx = int(vx + (100 + b * 160) * k); cy = int(vy + 100 * k)
		if f[cy - 10:cy + 10, cx - 10:cx + 10].mean() > 128:
			n |= 1 << b
	ids.append(n)
ids = np.array(ids)
steps = np.diff(ids)
runs, r = [], 1
for s in steps:
	if s == 0: r += 1
	else: runs.append(r); r = 1
runs.append(r)
print("%s: %d captures, frames %d..%d, %d shown; held %s; steps %s; %d backwards"
	% (cap.split("/")[-1], len(ids), ids[0], ids[-1], len(set(ids.tolist())),
	dict(sorted(collections.Counter(runs).items())),
	dict(sorted(collections.Counter(int(s) for s in steps if s != 0).items())),
	int((steps < 0).sum())))
