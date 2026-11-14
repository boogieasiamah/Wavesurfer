#!/usr/bin/env python3
"""Compare plugin renders of the WaveSurfer test pack (Gullfoss, Smooth Operator, WaveSurfer, ...).

Usage: compare_renders.py <original.wav> <render.wav> [<render2.wav> ...]

For each render: aligns it to the original (cross-correlation, handles plugin latency), then
prints the long-term output/input level per region, the K-weighted loudness change (BS.1770
filters, rate-independent bilinear design), and how much the low-end curve moves over time
(10th-90th percentile of the 60-150 Hz gain in 200 ms windows).
"""
import sys
import numpy as np
from scipy.io import wavfile
from scipy import signal

REGIONS = [(20, 60), (60, 150), (150, 400), (400, 1500), (1500, 5000), (5000, 16000)]


def load(path):
    fs, x = wavfile.read(path)
    if x.dtype.kind == "i":
        x = x.astype(np.float64) / float(np.iinfo(x.dtype).max)
    else:
        x = x.astype(np.float64)
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    return fs, x[:, :2]


def align(ref, y, max_lag):
    a, b = ref[:, 0] + ref[:, 1], y[:, 0] + y[:, 1]
    n = min(len(a), len(b), 10 * 48000)
    c = signal.correlate(b[:n], a[:n], mode="full", method="fft")
    mid = n - 1
    lo, hi = mid - max_lag, mid + max_lag + 1
    lag = int(np.argmax(np.abs(c[lo:hi]))) - max_lag
    if lag > 0:
        y = y[lag:]
    elif lag < 0:
        y = np.vstack([np.zeros((-lag, 2)), y])
    m = min(len(ref), len(y))
    return ref[:m], y[:m], lag


def k_weight(fs):
    # BS.1770 pre-filter (shelf) + RLB high-pass, from the analog prototypes (Mansbridge et al.)
    f0, g, q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    k = np.tan(np.pi * f0 / fs); vh = 10 ** (g / 20); vb = vh ** 0.4996667741545416
    a0 = 1 + k / q + k * k
    b1 = [(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0]
    a1 = [1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0]
    f0, q = 38.13547087602444, 0.5003270373238773
    k = np.tan(np.pi * f0 / fs)
    a2 = [1, 2 * (k * k - 1) / (1 + k / q + k * k), (1 - k / q + k * k) / (1 + k / q + k * k)]
    return (b1, a1), ([1, -2, 1], a2)


def loudness(x, fs):
    (b1, a1), (b2, a2) = k_weight(fs)
    y = signal.lfilter(b2, a2, signal.lfilter(b1, a1, x, axis=0), axis=0)
    return 10 * np.log10(np.sum(np.mean(y[fs:] ** 2, axis=0)) + 1e-30) - 0.691


def region_levels(x, fs):
    f, p = signal.welch(x[:, 0] + x[:, 1], fs, nperseg=16384)
    return [10 * np.log10(np.sum(p[(f >= lo) & (f < hi)]) + 1e-30) for lo, hi in REGIONS]


def low_swing(ref, y, fs):
    sos = signal.butter(4, [60, 150], btype="band", fs=fs, output="sos")
    a = signal.sosfilt(sos, ref[:, 0] + ref[:, 1]); b = signal.sosfilt(sos, y[:, 0] + y[:, 1])
    w = int(0.2 * fs); g = []
    for i in range(fs, len(a) - w, w):
        ea, eb = np.sum(a[i:i + w] ** 2), np.sum(b[i:i + w] ** 2)
        if ea > 1e-9:
            g.append(10 * np.log10(eb / ea + 1e-30))
    return (np.percentile(g, 90) - np.percentile(g, 10)) if g else 0.0


def main():
    fs, ref = load(sys.argv[1])
    base = region_levels(ref, fs); lref = loudness(ref, fs)
    print(f"original: {sys.argv[1]}  ({fs} Hz)")
    print(f"{'render':34s} lag   20-60  60-150 150-400 0.4-1.5k 1.5-5k  5-16k   loud  lowswing")
    for path in sys.argv[2:]:
        fs2, y = load(path)
        if fs2 != fs:
            print(f"{path}: sample rate {fs2} != {fs}, skipped"); continue
        r, y, lag = align(ref, y, int(0.1 * fs))
        lv = region_levels(y, fs)
        d = [b - a for a, b in zip(region_levels(r, fs), lv)]
        name = path.rsplit("/", 1)[-1][:34]
        print(f"{name:34s} {lag:4d} " + " ".join(f"{v:+6.1f}" for v in d)
              + f"  {loudness(y, fs) - loudness(r, fs):+5.2f}  {low_swing(r, y, fs):5.1f}")
    _ = base, lref


if __name__ == "__main__":
    main()
