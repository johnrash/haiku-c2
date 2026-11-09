import sys, numpy as np, wave
for path in sys.argv[1:]:
    w = wave.open(path); x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float)
    blk = 480; lv = np.array([x[i:i+blk].std() for i in range(0, len(x)-blk, blk)])
    on = np.nonzero(lv > lv.max() / 10)[0]
    x = x[(on[0] + 50) * blk:(on[-1] - 50) * blk]
    X = np.fft.fft(x); fc = np.fft.fftfreq(len(x), 1/48000); H = np.zeros(len(x)); H[(fc > 500) & (fc < 1000)] = 2
    z = np.fft.ifft(X * H)
    ph = np.unwrap(np.angle(z))
    t = np.arange(len(ph)) / 48000
    slope, icept = np.polyfit(t, ph, 1)
    res = ph - (slope * t + icept)
    # residual phase per 1 ms, then its steps between consecutive 5 ms means
    r5 = res[:len(res)//240*240].reshape(-1, 240).mean(axis=1)
    d = np.diff(r5)
    big = np.abs(d) > 0.5
    print("%s: %.3f Hz, residual phase spread %.2f rad; 5 ms steps > 0.5 rad: %d in %.1f s (%.1f/s); typical step %.2f rad" % (
        path.split("/")[-1], slope / (2*np.pi), res.std(), big.sum(), len(x)/48000, big.sum() / (len(x)/48000),
        np.median(d[big]) if big.any() else 0))
