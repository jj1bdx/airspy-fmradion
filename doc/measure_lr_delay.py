#!/usr/bin/env python3
"""Measure the airspy-fmradion end-to-end audio delay from a two-channel
reference recording.

Input: stereo WAV files whose left channel carries a reference receiver
(an analog FM radio, delay ~0) and whose right channel carries the
airspy-fmradion output, both captured by one ADC so the capture path
cancels.  The delay is the lag of the right channel relative to the left,
tracked over time by windowed cross-correlation.

The nominal capture rate is corrected by --fs-true, the ADC's measured
true sample rate (see loopback calibration in
doc/LATENCY_MEASUREMENT_20260725.md section 3).

Usage:
    measure_lr_delay.py [--fs-true HZ] [--csv OUT.csv] file.wav ...
"""
import argparse
import re
import sys

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt

WIN = 2.0        # correlation window, seconds
HOP = 0.25       # window hop, seconds
SPAN = 0.030     # +-search span around the coarse lag, seconds
BAND = (200.0, 4000.0)
RHO_MIN = 0.5


def bandpass(x, sr):
    sos = butter(4, [BAND[0] / (sr / 2), BAND[1] / (sr / 2)],
                 btype='band', output='sos')
    return sosfiltfilt(sos, x)


def coarse_lag(a, b, sr, maxlag=1.0):
    n = max(len(a), len(b))
    nfft = 1 << int(np.ceil(np.log2(2 * n)))
    cc = np.fft.irfft(np.fft.rfft(b, nfft) * np.conj(np.fft.rfft(a, nfft)), nfft)
    k = int(np.argmax(cc[:int(sr * maxlag)]))
    return k


def track_lag(a, b, sr, lag0):
    """Windowed lag of b relative to a, around lag0 samples."""
    w = int(WIN * sr)
    hop = int(HOP * sr)
    span = int(SPAN * sr)
    out = []
    for s in range(0, len(a) - w - lag0 - span, hop):
        ref = a[s:s + w]
        lo = s + lag0 - span
        if lo < 0:
            continue
        seg = b[lo:lo + w + 2 * span]
        if len(seg) < w + 2 * span:
            break
        nfft = 1 << int(np.ceil(np.log2(len(seg) + w)))
        cc = np.fft.irfft(np.fft.rfft(seg, nfft) *
                          np.conj(np.fft.rfft(ref, nfft)), nfft)[:2 * span]
        k = int(np.argmax(cc))
        if k == 0 or k == 2 * span - 1:
            continue
        y0, y1, y2 = cc[k - 1], cc[k], cc[k + 1]
        d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
        m = b[lo + k:lo + k + w]
        e = np.sqrt((ref ** 2).sum() * (m ** 2).sum())
        rho = y1 / e if e > 0 else 0.0
        out.append((s / sr + WIN / 2, lag0 - span + k + d, rho))
    return np.array(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--fs-true', type=float, default=None,
                    help='true ADC sample rate (default: the WAV header rate)')
    ap.add_argument('--csv')
    args = ap.parse_args()

    rows = []
    tracks = {}
    for path in args.files:
        x, sr = sf.read(path, always_2d=True)
        fs_true = args.fs_true or sr
        left = bandpass(x[:, 0], sr)
        right = bandpass(x[:, 1], sr)
        lag0 = coarse_lag(left, right, sr)
        tr = track_lag(left, right, sr, lag0)
        t, lag, rho = tr[:, 0], tr[:, 1] / fs_true * 1e3, tr[:, 2]
        good = rho > RHO_MIN
        # reject windows that also disagree grossly with the median
        med = np.median(lag[good])
        good &= np.abs(lag - med) < 25.0
        tg, lg = t[good], lag[good]
        slope = np.polyfit(tg, lg, 1)[0] if len(tg) > 3 else float('nan')
        m = re.search(r'-L(\d+)', path)
        rows.append(dict(file=path, L=int(m.group(1)) if m else -1,
                         dur=len(x) / sr, n=int(good.sum()), ntot=len(t),
                         median=np.median(lg), mean=lg.mean(), std=lg.std(),
                         lo=lg.min(), hi=lg.max(), slope=slope,
                         rho=np.median(rho[good])))
        tracks[rows[-1]['L']] = (tg, lg)

    rows.sort(key=lambda r: r['L'])
    print('%4s %7s %5s %9s %8s %8s %8s %8s %8s %6s' %
          ('-L', 'dur_s', 'wins', 'median_ms', 'mean_ms', 'std_ms',
           'min_ms', 'max_ms', 'ms/s', 'rho'))
    for r in rows:
        print('%4d %7.1f %2d/%-3d %9.2f %8.2f %8.3f %8.2f %8.2f %8.3f %6.3f' %
              (r['L'], r['dur'], r['n'], r['ntot'], r['median'], r['mean'],
               r['std'], r['lo'], r['hi'], r['slope'], r['rho']))

    if args.csv:
        import csv
        with open(args.csv, 'w', newline='') as fh:
            wtr = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
            wtr.writeheader()
            wtr.writerows(rows)
    return tracks


if __name__ == '__main__':
    main()
