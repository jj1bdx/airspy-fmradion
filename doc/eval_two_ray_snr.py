#!/usr/bin/env python3
# airspy-fmradion
# Post-discriminator SNR of a decode against a known-clean decode.
#
# Copyright (C) 2026 Kenji Rikitake, JJ1BDX
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Intended for the synthesized two-ray channels produced by
# make_two_ray_channel.py: decode the clean IQ file to get the reference
# audio, decode the channel-corrupted file to get the test audio, then
# compare them here.
#
# The comparison fits a least-squares FIR from the test audio to the
# reference and reports the residual power. Fitting a filter rather than a
# scalar gain absorbs the multipath filter's group delay -- which is a
# fractional number of audio samples and would otherwise dominate the
# residual -- along with any gain or frequency-response difference. What is
# left is what no linear time-invariant relation can explain: the nonlinear
# distortion and noise that multipath actually causes after FM demodulation.
#
# Usage:
#   ./eval_two_ray_snr.py --reference ref.wav test1.wav test2.wav ...
#
# See doc/MULTIPATH_FILTER_DESIGN_20260724.md section 17.

import argparse
import numpy as np
import soundfile as sf

FIR_LENGTH = 129  # long enough to absorb delay and any response tilt
SKIP_SECONDS = 3.0  # discard filter warm-up and decoder settling
DURATION_SECONDS = 12.0


def snr_db(ref, fs, path):
    x, fsx = sf.read(path)
    if fsx != fs:
        raise SystemExit("%s: sample rate %d != reference %d" % (path, fsx, fs))
    n = min(len(ref), len(x))
    s = int(SKIP_SECONDS * fs)
    e = min(n, s + int(DURATION_SECONDS * fs))
    if e <= s:
        raise SystemExit("%s: too short" % path)
    a = ref[s:e].mean(axis=1)
    b = x[s:e].mean(axis=1)
    windows = np.lib.stride_tricks.sliding_window_view(b, FIR_LENGTH)
    target = a[FIR_LENGTH // 2 : FIR_LENGTH // 2 + len(windows)]
    h, *_ = np.linalg.lstsq(windows, target, rcond=None)
    residual = target - windows @ h
    return 10.0 * np.log10(float(target @ target) / float(residual @ residual))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--reference", required=True,
                   help="audio decoded from the clean IQ file")
    p.add_argument("tests", nargs="+", help="audio decoded from channel files")
    args = p.parse_args()

    ref, fs = sf.read(args.reference, always_2d=True)
    for t in args.tests:
        print("%8.2f dB  %s" % (snr_db(ref, fs, t), t))


if __name__ == "__main__":
    main()
