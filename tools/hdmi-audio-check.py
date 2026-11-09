#!/usr/bin/env python3
# Analyzes a WAV recorded from the HDMI capture stick (arecord -D hw:1,0
# -f S16_LE -r 48000 -c 2): per channel the level, the frequency of the
# strongest tone (zero crossings) and discontinuities (sample to sample
# jumps much larger than a sine of that level and frequency can make).
# Usage: hdmi-audio-check.py <file.wav>
import math
import struct
import sys
import wave

w = wave.open(sys.argv[1])
rate = w.getframerate()
channels = w.getnchannels()
n = w.getnframes()
data = struct.unpack("<%dh" % (n * channels), w.readframes(n))
print("%s: %d frames, %d Hz, %d channels" % (sys.argv[1], n, rate, channels))
for c in range(channels):
	s = data[c::channels]
	peak = max(abs(x) for x in s)
	rms = math.sqrt(sum(x * x for x in s) / len(s))
	crossings = [i for i in range(1, len(s)) if s[i - 1] < 0 <= s[i]]
	freq = 0.0
	if len(crossings) > 2:
		freq = (len(crossings) - 1) * rate / (crossings[-1] - crossings[0])
	# a sine of amplitude A and frequency f changes by at most 2*pi*f/rate*A
	# per sample
	limit = 2 * math.pi * max(freq, 20) / rate * peak * 1.5 + 64
	jumps = [i for i in range(1, len(s)) if abs(s[i] - s[i - 1]) > limit]
	print("  channel %d: peak %d, rms %.0f, %.1f Hz, %d jumps%s" % (c, peak,
		rms, freq, len(jumps),
		(" (first at %s)" % ", ".join("%.3f s" % (j / rate) for j in jumps[:5]))
			if jumps else ""))
