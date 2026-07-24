#!/usr/bin/env python3
# airspy-fmradion
# Synthesize a two-ray multipath channel on a recorded IQ file.
#
# Copyright (C) 2026 Kenji Rikitake, JJ1BDX
# SPDX-License-Identifier: GPL-3.0-or-later
#
# y[n] = x[n] + a(t) * exp(j*theta(t)) * x[n - tau]
#
# a and theta are constant by default. --fade-depth swings a sinusoidally in
# dB, and --doppler rotates theta linearly; together they produce a
# time-varying channel that can be made to cross a = 1, i.e. to move in and
# out of the non-minimum-phase condition during the run.
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
#   # static channel
#   ./make_two_ray_channel.py --input test-files/piano_iqtest.wav \
#       --amp 0.5 --delay 5.0 --phase 0.7 --outdir test-files
#
#   # fade crossing a = 1, with 10 Hz Doppler (~30 m/s at 100 MHz)
#   ./make_two_ray_channel.py --input test-files/piano_iqtest.wav \
#       --amp 0.9 --delay 3.0 --fade-depth 6 --fade-rate 2 --doppler 10
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


def fmt(value):
    """Format a number for use in a file name."""
    return ("%g" % value).replace(".", "p").replace("-", "m")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", required=True, help="clean IQ WAV file")
    p.add_argument("--amp", type=float, required=True,
                   help="echo amplitude a; use >1 for a non-minimum-phase channel")
    p.add_argument("--delay", type=float, required=True,
                   help="echo delay in microseconds (may be fractional)")
    p.add_argument("--phase", type=float, default=0.0,
                   help="echo phase theta in radians (default 0)")
    p.add_argument("--fade-depth", type=float, default=0.0,
                   help="peak-to-peak swing of the echo amplitude in dB; 0 "
                        "(default) keeps a constant, i.e. a static channel")
    p.add_argument("--fade-rate", type=float, default=1.0,
                   help="rate of the amplitude fade in Hz (default 1.0); "
                        "only meaningful with --fade-depth")
    p.add_argument("--doppler", type=float, default=0.0,
                   help="rotate the echo phase at this rate in Hz. This is "
                        "what a moving receiver actually produces, and it "
                        "fades the composite envelope without changing a")
    p.add_argument("--outdir", default="test-files", help="output directory")
    p.add_argument("--tag", default=None, help="override the output basename")
    args = p.parse_args()

    x, fs = sf.read(args.input, always_2d=True)
    if x.shape[1] != 2:
        raise SystemExit("expected a 2-channel (I/Q) file")
    z = (x[:, 0] + 1j * x[:, 1]).astype(np.complex128)

    t = np.arange(len(z)) / float(fs)

    # Echo amplitude. The swing is applied in dB because that is how fade
    # depth is normally specified; --fade-depth 0 leaves a exactly constant,
    # so an invocation without the fade options is unchanged.
    if args.fade_depth != 0.0:
        swing_db = 0.5 * args.fade_depth * np.sin(2 * np.pi * args.fade_rate * t)
        amp = args.amp * 10.0 ** (swing_db / 20.0)
    else:
        amp = np.full(len(z), args.amp)

    # Echo phase. A constant Doppler shift rotates the echo relative to the
    # direct path, which is the physical mechanism behind fast fading: the two
    # rays drift in and out of opposition even at constant a.
    theta = args.phase + 2 * np.pi * args.doppler * t

    delay_samples = args.delay * 1e-6 * fs
    echo = amp * np.exp(1j * theta) * fractional_delay(z, delay_samples)
    y = z + echo

    # Match the IF AGC's unity target so that the CM reference R2 = 1 holds.
    # This is a single global scaling, not a tracking AGC: the receiver's own
    # IfSimpleAgc must be left to follow the fade, as it would on the air.
    y *= 1.0 / np.mean(np.abs(y))

    peak = np.abs(y).max()
    if peak > 1.0:
        print("note: peak envelope %.3f exceeds 1.0; float WAV has headroom" % peak)

    tag = args.tag or "%s-a%s-t%sus" % (
        os.path.splitext(os.path.basename(args.input))[0],
        fmt(args.amp), fmt(args.delay),
    )
    if args.tag is None and args.fade_depth != 0.0:
        tag += "-f%sdB%sHz" % (fmt(args.fade_depth), fmt(args.fade_rate))
    if args.tag is None and args.doppler != 0.0:
        tag += "-d%sHz" % fmt(args.doppler)

    out = os.path.join(args.outdir, tag + ".wav")
    sf.write(out, np.column_stack([y.real, y.imag]).astype(np.float32), fs,
             subtype="FLOAT")
    print("wrote %s  (%d ch, %d Hz, %.1f s, tau=%g us = %.3f samples)"
          % (out, 2, fs, len(y) / fs, args.delay, delay_samples))
    if args.fade_depth == 0.0 and args.doppler == 0.0:
        print("  static channel: a=%g, theta=%g rad" % (args.amp, args.phase))
    else:
        above = float(np.mean(amp > 1.0)) * 100.0
        print("  a: %.3f to %.3f (%.1f dB p-p at %g Hz), %.1f%% of the run is "
              "non-minimum-phase (a > 1)"
              % (amp.min(), amp.max(), args.fade_depth, args.fade_rate, above))
        print("  theta: %g rad + %g Hz Doppler" % (args.phase, args.doppler))
        env = np.abs(y)
        print("  composite envelope: %.3f to %.3f, %.1f dB peak-to-null"
              % (env.min(), env.max(),
                 20 * np.log10(env.max() / max(env.min(), 1e-12))))


if __name__ == "__main__":
    main()
