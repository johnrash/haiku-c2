#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# audio-gap-check.py <wav> [threshold]: finds the clicks in a recording of a
# steady tone (audiotest's, played on a board and recorded on the laptop
# through a cable, milestone M82). Takes out the tone and its first harmonics
# in the frequency domain, then looks at what is left in 0.5 ms steps: a
# lost or late USB packet leaves about the tone's own level there, against
# the noise floor elsewhere. Prints each click (time, length, level) and a
# count; the tone's start and end are left out.
import sys
import wave

import numpy as np

w = wave.open(sys.argv[1])
rate = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float)
if w.getnchannels() > 1:
	x = x.reshape(-1, w.getnchannels())[:, 0]
threshold = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0

# where the tone plays: 10 ms blocks above a tenth of the loudest
block = rate // 100
levels = np.array([x[i:i + block].std() for i in range(0, len(x) - block, block)])
on = np.nonzero(levels > levels.max() / 10)[0]
start, end = (on[0] + 50) * block, (on[-1] - 50) * block
x = x[start:end]

# the tone's frequency, from a Hann-windowed spectrum of the whole
spectrum = np.abs(np.fft.rfft(x * np.hanning(len(x))))
freqs = np.fft.rfftfreq(len(x), 1 / rate)
peak = np.argmax(spectrum[freqs > 20]) + np.count_nonzero(freqs <= 20)
tone = freqs[peak]

# taken out in overlapping 1 s windows (square root of Hann both ways, half
# overlapping, which adds back up to the signal), so that it doesn't leak
size = rate
hop = size // 2
window = np.sqrt(np.hanning(size + 1)[:size])
bins = np.fft.rfftfreq(size, 1 / rate)
cut = np.zeros(len(bins), dtype=bool)
for n in range(1, 6):
	cut |= np.abs(bins - n * tone) < 6
residual = np.zeros(len(x) + size)
padded = np.concatenate([x, np.zeros(size)])
for i in range(0, len(x), hop):
	part = np.fft.rfft(padded[i:i + size] * window)
	part[cut] = 0
	residual[i:i + size] += np.fft.irfft(part, size) * window
residual = residual[:len(x)]
# the first and last half window add up to less
residual[:hop] = 0
residual[-hop:] = 0

step = rate // 2000
rms = np.sqrt(np.mean(residual[:len(residual) // step * step]
	.reshape(-1, step) ** 2, axis=1))
floor = np.median(rms)
loud = rms > floor * threshold
print("tone %.2f Hz, level %.0f rms, residual floor %.1f, %.1f s checked"
	% (tone, x.std(), floor, len(x) / rate))
clicks = 0
i = 0
while i < len(loud):
	if not loud[i]:
		i += 1
		continue
	j = i
	while j + 1 < len(loud) and loud[j + 1:j + 21].any():
		j += 1
	clicks += 1
	print("  %8.3f s: %.1f ms, up to %.0f" % ((start / rate) + i * step / rate,
		(j - i + 1) * step * 1000 / rate, rms[i:j + 1].max()))
	i = j + 1
print("%d clicks" % clicks)
