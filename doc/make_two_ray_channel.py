#!/usr/bin/env python3
# airspy-fmradion
# Synthesize a two-ray multipath channel on a recorded IQ file.
#
# Copyright (C) 2026 Kenji Rikitake, JJ1BDX
# SPDX-License-Identifier: GPL-3.0-or-later
#
# y[n] = x[n] + a * exp(j*theta) * x[n - tau]
#
# tau is given in microseconds and may be fractional; the delayed copy is
# produced by a windowed-sinc fractional delay so that sub-sample echoes are
# represented exactly for a band-limited signal (see
# doc/MULTIPATH_FILTER_DESIGN_20260724.md Part I section 3).
#
# The output is renormalized so that the mean envelope is 1.0, matching what
# the IF AGC delivers to MultipathFilter.
#
# Usage:
#   ./make_two_ray_channel.py --input test-files/piano_iqtest.wav \
#       --amp 0.5 --delay 5.0 --phase 0.7 --outdir test-files
#
# See doc/MULTIPATH_FILTER_DESIGN_20260724.md section 17.

import argparse
import os
import numpy as np
import soundfile as sf

# Half-length of the fractional-delay interpolator, in samples.
SINC_HALF = 32


def fractional_delay(x, delay_samples):
    """Delay a complex signal by delay_samples (may be fractional)."""
    whole = int(np.floor(delay_samples))
    frac = delay_samples - whole
    if frac == 0.0:
        return np.concatenate([np.zeros(whole, dtype=x.dtype), x])[: len(x)]
    # Windowed-sinc fractional-delay FIR, centred on SINC_HALF.
    n = np.arange(-SINC_HALF, SINC_HALF + 1)
    h = np.sinc(n - frac) * np.blackman(2 * SINC_HALF + 1)
    h /= h.sum()
    y = np.convolve(x, h.astype(x.dtype))[SINC_HALF : SINC_HALF + len(x)]
    return np.concatenate([np.zeros(whole, dtype=x.dtype), y])[: len(x)]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", required=True, help="clean IQ WAV file")
    p.add_argument("--amp", type=float, required=True,
                   help="echo amplitude a; use >1 for a non-minimum-phase channel")
    p.add_argument("--delay", type=float, required=True,
                   help="echo delay in microseconds (may be fractional)")
    p.add_argument("--phase", type=float, default=0.0,
                   help="echo phase theta in radians (default 0)")
    p.add_argument("--outdir", default="test-files", help="output directory")
    p.add_argument("--tag", default=None, help="override the output basename")
    args = p.parse_args()

    x, fs = sf.read(args.input, always_2d=True)
    if x.shape[1] != 2:
        raise SystemExit("expected a 2-channel (I/Q) file")
    z = (x[:, 0] + 1j * x[:, 1]).astype(np.complex128)

    delay_samples = args.delay * 1e-6 * fs
    echo = args.amp * np.exp(1j * args.phase) * fractional_delay(z, delay_samples)
    y = z + echo

    # Match the IF AGC's unity target so that the CM reference R2 = 1 holds.
    y *= 1.0 / np.mean(np.abs(y))

    peak = np.abs(y).max()
    if peak > 1.0:
        print("note: peak envelope %.3f exceeds 1.0; float WAV has headroom" % peak)

    tag = args.tag or "%s-a%s-t%sus" % (
        os.path.splitext(os.path.basename(args.input))[0],
        ("%g" % args.amp).replace(".", "p"),
        ("%g" % args.delay).replace(".", "p"),
    )
    out = os.path.join(args.outdir, tag + ".wav")
    sf.write(out, np.column_stack([y.real, y.imag]).astype(np.float32), fs,
             subtype="FLOAT")
    print("wrote %s  (%d ch, %d Hz, %.1f s, a=%g, tau=%g us = %.3f samples, "
          "theta=%g rad)" % (out, 2, fs, len(y) / fs, args.amp, args.delay,
                             delay_samples, args.phase))


if __name__ == "__main__":
    main()
