import sys, numpy as np, wave
w = wave.open(sys.argv[1]); x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float)
blk = 480; lv = np.array([x[i:i+blk].std() for i in range(0, len(x)-blk, blk)])
on = np.nonzero(lv > lv.max() / 10)[0]
x = x[(on[0] + 50) * blk:(on[-1] - 50) * blk]
X = np.abs(np.fft.rfft(x * np.hanning(len(x)))); f = np.fft.rfftfreq(len(x), 1/48000)
k = np.argmax(X[f > 20]) + np.count_nonzero(f <= 20)
a, b, c = np.log(X[k-1:k+2]); p = 0.5 * (a - c) / (a - 2*b + c)
print("%.1f s: recorded tone %.3f Hz (sent 761.905), rms %.0f, peak %.0f" % (len(x)/48000, (k + p) * 48000 / len(x), x.std(), np.abs(x).max()))
Xc = np.fft.fft(x); fc = np.fft.fftfreq(len(x), 1/48000); H = np.zeros(len(x)); H[(fc > 600) & (fc < 900)] = 2
z = np.fft.ifft(Xc * H); amp = np.abs(z); inst = np.diff(np.unwrap(np.angle(z))) * 48000 / (2*np.pi)
f50 = inst[:len(inst)//2400*2400].reshape(-1, 2400).mean(axis=1)
med = np.median(f50)
print("  per 50 ms: median %.2f, min %.2f, max %.2f; windows off by > 1 Hz: %d of %d" % (med, f50.min(), f50.max(), np.count_nonzero(np.abs(f50 - med) > 1), len(f50)))
a1 = amp[:len(amp)//48*48].reshape(-1, 48).min(axis=1)
print("  1 ms amplitude dips below 60%% of median: %d" % np.count_nonzero(a1 < 0.6 * np.median(a1)))
idx = np.nonzero(a1 < 0.6 * np.median(a1))[0]
if len(idx):
    print("  dips at (s):", " ".join("%.3f" % (i / 1000) for i in idx[:30]))
