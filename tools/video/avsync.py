# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
# avsync.py <recording> <crop w:h:x:y>: times of the white flashes in the
# video area and of the beep onsets in the audio; prints each flash's audio
# offset (positive: sound after picture) and how it drifts
import subprocess, sys, tempfile, os, wave
import numpy as np
from PIL import Image
video, crop = sys.argv[1:3]
d = tempfile.mkdtemp()
subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", video,
	"-vf", "crop=%s,scale=64:36,format=gray" % crop, "-f", "rawvideo",
	os.path.join(d, "v.raw")], check=True)
subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", video,
	"-vn", "-ac", "1", "-ar", "48000", "-f", "s16le", os.path.join(d, "a.raw")],
	check=True)
v = np.fromfile(os.path.join(d, "v.raw"), np.uint8).reshape(-1, 36 * 64)
bright = v.mean(axis=1)
# each stream's first time stamp (hdmi-record-av.sh keeps wall clock time
# stamps), so that picture and sound times compare
starts = {}
for line in subprocess.run(["ffprobe", "-v", "error", "-show_entries",
		"stream=codec_type,start_time", "-of", "csv=p=0", video],
		capture_output=True, text=True).stdout.split():
	kind, start = line.split(",")
	starts[kind] = float(start)
print("stream starts: video %.3f, audio %.3f (audio %+d ms)" % (
	starts["video"], starts["audio"],
	(starts["audio"] - starts["video"]) * 1000))
# frame times: 60 fps capture
times = starts["video"] + np.arange(len(bright)) / 60.0
flashes = [times[i] for i in range(1, len(bright))
	if bright[i] > 200 and bright[i - 1] <= 200]
a = np.fromfile(os.path.join(d, "a.raw"), np.int16).astype(float)
win = 240		# 5 ms
rms = np.sqrt(np.convolve(a * a, np.ones(win) / win, mode="valid")[::win])
level = rms > max(2000, rms.max() * 0.3)
beeps = [starts["audio"] + i * win / 48000.0 for i in range(1, len(level))
	if level[i] and not level[i - 1]]
offsets = []
for f in flashes:
	near = [b - f for b in beeps if abs(b - f) < 0.5]
	if near:
		offsets.append((f, min(near, key=abs)))
print("%s: %d flashes, %d beeps, %d paired" % (os.path.basename(video),
	len(flashes), len(beeps), len(offsets)))
for f, o in offsets[::5]:
	print("  t %5.1f s: sound %+4d ms" % (f - starts["video"], o * 1000))
if offsets:
	o = np.array([x[1] for x in offsets]) * 1000
	print("  offset ms: first %+d, last %+d, min %+d, max %+d, spread %d"
		% (o[0], o[-1], o.min(), o.max(), o.max() - o.min()))
