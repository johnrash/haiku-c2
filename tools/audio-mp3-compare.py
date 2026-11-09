#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# audio-mp3-compare.py <recording.wav> <file.mp3>: compares a 48 kHz mono
# recording of a played MP3 (the adapter cabled to the laptop's Y-247A,
# `sudo arecord -D hw:2,0 -f S16_LE -r 48000 -c 1`) with the decoded file
# (milestone M82 (docs/milestones.md)). Follows the recording's lag behind the file every 2 s: a
# step up is sound played twice (usb_audio repeated buffers: 80 ms at
# 44.1 kHz, 84 at 48), a step down sound skipped. Then compares band energies
# per 20 ms with what the file predicts and lists dropouts (much quieter than
# the file) and noise bursts (much louder in six bands or more). Test tones
# can't show a repeated buffer when they have whole periods per buffer.
import subprocess
import sys
import wave

import numpy as np

RATE = 48000
rec_path, mp3_path = sys.argv[1], sys.argv[2]
w = wave.open(rec_path)
rec = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16) \
	.astype(np.float64)
raw = subprocess.run(["ffmpeg", "-v", "error", "-i", mp3_path, "-f", "f32le",
	"-ac", "2", "-ar", str(RATE), "-"], capture_output=True).stdout
ref2 = np.frombuffer(raw, dtype=np.float32).reshape(-1, 2).astype(np.float64)


def xcorr_lag(a, b, max_lag=None, positive=False):
	# lag such that a[n] ~ b[n - lag]; positive: the recording chain doesn't
	# invert, so only a positive match counts (a negative one misaligned)
	n = 1
	while n < len(a) + len(b):
		n *= 2
	c = np.fft.irfft(np.fft.rfft(a, n) * np.conj(np.fft.rfft(b, n)), n)
	if max_lag is not None:
		c = np.concatenate([c[-max_lag:], c[:max_lag + 1]])
		k = np.argmax(c if positive else np.abs(c))
		return k - max_lag, c[k] / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-9)
	k = np.argmax(c if positive else np.abs(c))
	lag = k if k < n // 2 else k - n
	return lag, c[k] / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-9)


# coarse offset: the recording's first 40 s of sound against the file's start
level = np.array([rec[i:i + 4800].std() for i in range(0, len(rec) - 4800, 4800)])
start = np.argmax(level > level.max() / 30) * 4800
seg = rec[start:start + 40 * RATE]
best = None
for name, sig in (("left", ref2[:, 0]), ("right", ref2[:, 1]),
		("both", ref2.sum(axis=1))):
	lag, c = xcorr_lag(seg, sig[:60 * RATE], positive=True)
	if best is None or c > best[2]:
		best = (name, lag + start, c)
offset = best[1]
# least squares mix of the two channels on the aligned stretch
i0 = max(offset, start)
n = min(40 * RATE, len(ref2) - (i0 - offset))
A = ref2[i0 - offset:i0 - offset + n]
mix, *_ = np.linalg.lstsq(A, rec[i0:i0 + n], rcond=None)
ref = ref2 @ mix
print("%s: aligned to the file at %.3f s in the recording, best channel %s "
	"(correlation %.2f), mix %.2f L + %.2f R" % (rec_path.split("/")[-1],
	offset / RATE, best[0], best[2], mix[0], mix[1]))

# lag over time from 1 ms energy envelopes of the differentiated signals:
# every 2 s, the best match of 4 s within 300 ms of the last one; then the
# waveform within 1 ms of that for the exact sample
F1 = 48


def envelope(x):
	d = np.diff(x, prepend=0)
	e = np.log10((d[:len(d) // F1 * F1].reshape(-1, F1) ** 2).sum(axis=1) + 1e-9)
	return e


erec = envelope(rec / np.abs(rec).max())
eref = envelope(ref / np.abs(ref).max())
points = []
last = offset // F1
for t in range(2, int(len(ref) / RATE) - 4, 2):
	f0 = t * 1000
	b = eref[f0:f0 + 4000]
	lo = f0 + last - 300
	if lo < 0 or lo + 4600 > len(erec):
		continue
	a = erec[lo:lo + 4600]
	b = (b - b.mean()) / (b.std() + 1e-9)
	scores = []
	for k in range(0, 601):
		w = a[k:k + 4000]
		scores.append(np.dot((w - w.mean()) / (w.std() + 1e-9), b) / 4000)
	k = int(np.argmax(scores))
	if scores[k] < 0.3:
		continue
	lag_ms = last - 300 + k
	# exact sample from the waveform within 1 ms either side
	r0 = f0 * F1 + lag_ms * F1
	d, c = xcorr_lag(rec[r0:r0 + RATE], ref[f0 * F1:f0 * F1 + RATE], 60)
	points.append((t, lag_ms * F1 + d, scores[k]))
	last = lag_ms
pts = np.array(points, dtype=float)
pts[:, 1] -= offset
print("  lag points: %d, lag from %+.0f to %+.0f samples, mean envelope "
	"match %.2f" % (len(pts), pts[:, 1].min(), pts[:, 1].max(), pts[:, 2].mean()))
steps = np.diff(pts[:, 1])
for k in np.nonzero(np.abs(steps) > 200)[0][:10]:
	print("  lag step %+.0f samples (%+.1f ms) between %.0f and %.0f s of the "
		"file" % (steps[k], steps[k] / 48, pts[k, 0], pts[k + 1, 0]))


def lag_at(t):
	return np.interp(t, pts[:, 0] + 0.5, pts[:, 1])


# band energies per 20 ms window
win, hop = 960, 480
edges = np.geomspace(80, 20000, 21)
freqs = np.fft.rfftfreq(win, 1 / RATE)
band = np.digitize(freqs, edges) - 1
hann = np.hanning(win)


def bands(x):
	p = np.abs(np.fft.rfft(x * hann)) ** 2
	return np.array([p[band == k].sum() for k in range(len(edges) - 1)])


noise = []
for i in range(0, max(start - win - 4800, 0), hop):
	noise.append(bands(rec[i:i + win]))
rows = []
t = 0
while True:
	ri = t * hop + offset
	ri += int(round(lag_at(t * hop / RATE)))
	fi = t * hop
	if fi + win > len(ref) or ri + win > len(rec):
		break
	if ri >= 0:
		rows.append((fi / RATE, bands(rec[ri:ri + win]), bands(ref[fi:fi + win])))
	t += 1
times = np.array([r[0] for r in rows])
R = np.array([r[1] for r in rows]) + 1e-3
F = np.array([r[2] for r in rows]) + 1e-12
nf = np.median(np.array(noise), axis=0) if len(noise) else np.full(R.shape[1], 1e-3)
# each band's gain from file to recording, from windows where the file is
# well above the recording's noise in that band
lr = 10 * np.log10(R / F)
fl = 10 * np.log10(F)
gain = np.zeros(R.shape[1])
ok_band = np.zeros(R.shape[1], bool)
for k in range(R.shape[1]):
	good = R[:, k] > 30 * nf[k]
	if np.count_nonzero(good) > 200:
		gain[k] = np.median(lr[good, k])
		ok_band[k] = True
pred = F * 10 ** (gain / 10) + nf
dev = 10 * np.log10(R / pred)
tot_r = (R[:, ok_band]).sum(axis=1)
tot_p = (pred[:, ok_band]).sum(axis=1)
loud = tot_p > 100 * nf[ok_band].sum()
drop = loud & (10 * np.log10(tot_r / tot_p) < -10)
burst = np.count_nonzero((dev > 10) & (R > 30 * nf), axis=1) >= 6
print("  bands used: %d of %d (%.0f-%.0f Hz); windows: %d, loud %d" % (
	np.count_nonzero(ok_band), len(ok_band), edges[np.argmax(ok_band)],
	edges[len(ok_band) - np.argmax(ok_band[::-1])], len(times),
	np.count_nonzero(loud)))
print("  median deviation in used bands: %.1f dB, 99th percentile %.1f dB" % (
	np.median(np.abs(dev[:, ok_band])), np.percentile(np.abs(dev[:, ok_band]), 99)))


def events(mask):
	out, i = [], 0
	idx = np.nonzero(mask)[0]
	while i < len(idx):
		j = i
		while j + 1 < len(idx) and idx[j + 1] - idx[j] <= 3:
			j += 1
		out.append((times[idx[i]], times[idx[j]] + win / RATE, idx[i], idx[j]))
		i = j + 1
	return out


for name, mask, m in (("dropouts", drop, lambda a, b: "%.0f dB down" % (
		10 * np.log10(tot_r[a:b + 1].sum() / tot_p[a:b + 1].sum()))),
		("noise bursts", burst, lambda a, b: "up to %.0f dB over, bands %s" % (
		dev[a:b + 1].max(), ",".join(str(k) for k in
		np.nonzero(np.any(dev[a:b + 1] > 12, axis=0))[0])))):
	ev = events(mask)
	print("  %s: %d" % (name, len(ev)))
	for s, e, a, b in ev[:25]:
		print("    %7.2f-%7.2f s of the file (%3.0f ms): %s" % (s, e,
			(e - s) * 1000, m(a, b)))
