# Why the 89.7 MHz recording decodes differently on dev and 20260716-0 (2026-07-27)

Decoding `test-files/AirSpy_20260727_125800Z_89700kHz_IQ.wav` with `-E 36`
produces a noticeably different audio file on the current `dev` build
(`b6b7d40`) than on the build at tag `20260716-0` (`dbca134`). The multipath
filter was the suspect. It is not the cause: the filter behaves identically in
the two builds on this recording, and the entire difference comes from the
pilot PLL — almost all of it from one 0.3-second window at the very start of
the decode.

## Executive summary

- **The multipath filter is exonerated.** Its coefficient vectors in the two
  builds agree to a median relative error of **−99.95 dB** over all 188 dumps
  of the 200-second decode, the off-reference energy per dump matches to
  0.000 dB, and neither build ever tripped a divergence reset (§3).
- **99.64 % of the difference energy lies between t = 0.20 s and t = 0.50 s.**
  In that window the old build still emits mono while `dev` already emits full
  stereo, because the PLL lock-decision time was shortened from 0.5 s to 0.2 s
  in `ed5ff93`. After ~0.7 s the two outputs agree to
  **−128 dBFS**, i.e. 111 dB below program level (§5).
- **Proved by construction, not by inference.** `dev` with *only*
  `PilotPhaseLock.{h,cpp}` reverted to `20260716-0` reproduces the old build to
  −149.99 dBFS. `dev` with *only* the old 0.5 s lock delay restored is
  **bit-identical to `dev`** after t = 0.5 s (§4).
- **The `fast_atan2f` → `std::atan2` phase-detector swap contributes nothing**
  measurable: −171.04 dBFS between `dev` and `dev` built with the old detector
  (§4).
- **The mono channel is untouched by any of this.** M = (L+R)/2 differs by
  −133.8 dB between the builds, in every configuration tested; that residual is
  the FIR low-pass rewrite of `LPF_VOLK_20260725.md` and is inaudible (§4, §6).
- **Two residual 60–70 ms bursts** at t = 52.18 s and t = 130.40 s carry
  the remainder. They come from the PLL loop retune (widened phasor biquad,
  rescaled PI gains), not from the lock delay (§6).
- **For scale:** what the multipath filter itself does to this recording is
  −22.6 dBFS — only 5.7 dB below the mono signal. The build-to-build steady-state
  difference is roughly 105 dB smaller than the filter's own effect (§7).

## 1. Method

Four builds, all `-O3 -ftree-vectorize`, all decoding the same file:

| build | source | purpose |
|---|---|---|
| `old` | tag `20260716-0` (`dbca134`), pristine | reference |
| `new` | `dev` (`b6b7d40`), pristine | subject |
| `old-dbg` / `new-dbg` | as above + stderr-only instrumentation | coefficient and divergence logging |
| `hyb-pll` | `dev` + `PilotPhaseLock.{h,cpp}` and `Utility.h` from `20260716-0` | isolate the PLL |
| `hyb-atan` | `dev` + `Utility::fast_atan2f` phase detector | isolate the phase detector |
| `hyb-lock` | `dev` + old `m_lock_delay` (`15.0 / bandwidth_pll`) | isolate the lock delay |

The debug builds add two compile-time flags that print to stderr and touch no
DSP arithmetic: `DEBUG_MF_RESET` (one line per multipath filter reset in
`FmDecoder::process()`) and `DEBUG_MF_PEAK` (one line per block with the peak
filter output magnitude and the peak `|m_error|` — the two quantities the
divergence test in `MultipathFilter::process()` examines). They are combined
with the existing `COEFF_MONITOR` and `DEBUG_MULTIPATH_FILTER` flags of
`doc/MF_DEBUG_CODE_20260726.md`, injected through `EXTRA_FLAGS` so the working
tree stays clean. The hybrid builds live in throwaway `git worktree`
checkouts; nothing in the repository was modified for this measurement.

The decode command, for every build:

```sh
airspy-fmradion -m fm -t filesource -E 36 \
  -c freq=89700000,srate=384000,\
filename=test-files/AirSpy_20260727_125800Z_89700kHz_IQ.wav,wav,format=FLOAT \
  -q -G out.wav
```

`filesource` paces itself to real time, so each decode takes the full 200 s.
The output is 48 kHz float32 stereo, 9,599,899 frames (199.998 s). Both builds
produce identical file lengths and identical channel levels (L −17.02 dBFS,
R −17.07 dBFS), and cross-correlation confirms zero sample offset between
them, so every comparison below is a straight sample-by-sample subtraction.

## 2. What the difference looks like

Whole-file, `old` versus `new`, both at `-E 36`:

| quantity | value |
|---|---|
| RMS of (old − new), L and R | **−52.24 dBFS** |
| difference-to-signal ratio | −35.2 dB |
| correlation coefficient | 0.999850 |
| M = (L+R)/2, difference relative to M | **−133.82 dB** |
| S = (L−R)/2, difference relative to S | **−25.65 dB** |

The difference is therefore almost entirely in the stereo difference channel:
the mono sum matches at the float noise floor. That already points away from
the multipath filter, which sits upstream of the FM discriminator and cannot
affect S without affecting M.

The same comparison with the multipath filter **switched off entirely** (no
`-E`) gives −52.23 dBFS, S difference −23.61 dB — the same difference,
unchanged. Whatever produces it does not need the multipath filter to be
running.

Two more properties of the S difference rule out a simple stereo gain or phase
error:

- A least-squares fit S_new ≈ k · S_old gives k = 0.999952 and removes
  **0.00 %** of the difference energy.
- The sub-sample delay between the two S channels is +0.0001 samples
  (0.001 µs).
- Magnitude-squared coherence between the two S channels is > 0.99 in every
  band below 15 kHz, and collapses to 0.26 in 16–20 kHz and to ~0 above
  20 kHz — i.e. the two signals differ only in the region where the 19 kHz
  pilot and its recovery live.

## 3. The multipath filter behaves identically

Coefficients were dumped every 200 blocks (188 dumps over the run, blocks
0…37400) from both instrumented builds and compared tap by tap at matching
block numbers.

| measurement | old | new |
|---|---|---|
| coefficient dumps | 188 | 188 |
| reference tap `c[109]` exactly `1+0j` | yes, every dump | yes, every dump |
| off-reference energy, median | −1.743 dB | −1.743 dB |
| strongest tap (last 10 dumps) | 108, +2.60 µs, −8.19 dB | 108, +2.60 µs, −8.19 dB |
| post-cursor / pre-cursor split | 91.66 % / 8.34 % | 91.66 % / 8.34 % |
| `mf_error` mean / std | −0.000206 / 0.026980 | −0.000206 / 0.026980 |
| peak filter output magnitude | 1.39115 | 1.39115 |
| peak `|m_error|` | 1.00000 | 1.00000 |
| divergence resets (`MF_RESET`) | **0** | **0** |

Per-dump comparison of the two coefficient vectors:

```
||c_old - c_new|| / ||c_old||:  median -99.954 dB,  last dump -98.340 dB
off-reference energy difference: median 0.000 dB, range [-0.000, +0.000] dB
```

The per-block peak-output series is identical in 36,748 of 37,400 blocks; the
652 that differ do so in the sixth significant digit (e.g. 1.13114 versus
1.13113). This is float rounding, not a different trajectory — the CMA loop is
self-correcting on this signal and does not amplify the perturbation.

Three specific changes to `MultipathFilter` between the two revisions were
checked individually:

- **Alpha order-scaling** (`MultipathFilter.cpp:72`). At `-E 36` the filter
  order is `36 * 4 + 1 = 145`, which is exactly `alpha_reference_order`. The
  scaling is then `0.1 * 145.0 / 145.0`, which is not merely mathematically but
  *bit-exactly* `0.1` in IEEE-754 double (`0x1.999999999999ap-4` either way),
  and `std::min(0.1, 0.5)` leaves it alone. **At 36 stages this change is a
  no-op.** It only does anything at other `-E` values.
- **Divergence limit** (`MultipathFilter.h`, `divergence_limit = 10.0f`). The
  error-based test `|1 − |output|²| ≤ 10` is the tighter of the two and fires
  at |output| = √11 ≈ 3.32; the component test allows up to 10 per component.
  The measured peak over the whole run is **1.39** in both builds, and the peak
  `|m_error|` is 1.0 (which is what `1 − |output|²` gives when the output is
  near zero, not a divergence). Neither build came within 7 dB of the trip
  point, and the old `isfinite`-only test would not have fired either. The
  change is inert on this recording.
- **Delay-line rewrite and the incremental state-power sum.** The ring buffer
  presents the same taps in the same order as the old `emplace_back` /
  `erase(begin())` vector; the only change is that `m_window` is no longer
  always aligned to the allocation base, so VOLK may pick a different kernel.
  The state-power sum moved from a per-update float VOLK reduction to a
  `double` running sum, which perturbs `m_mu` by order 1e-7 relative on every
  update. Both are visible in the sixth-digit differences above and in nothing
  else.

## 4. Bisecting the difference by construction

Every row is a whole-file RMS of the sample-by-sample difference, `-E 36`, and
the same figure recomputed after discarding the first 0.5 s.

| A | B | RMS(A−B) | after 0.5 s | M diff/M | S diff/S |
|---|---|---|---|---|---|
| old | new | −52.24 dBFS | −76.22 dBFS | −133.82 dB | −25.65 dB |
| old | `dev` + old PLL | **−149.99 dBFS** | −149.98 dBFS | −133.82 dB | −129.02 dB |
| new | `dev` + old PLL | −52.24 dBFS | — | −155.12 dB | −25.66 dB |
| new | `dev` + `fast_atan2f` | **−171.04 dBFS** | −171.07 dBFS | −163.93 dB | −144.87 dB |
| old | `dev` + `fast_atan2f` | −52.24 dBFS | −76.22 dBFS | −133.82 dB | −25.65 dB |
| new | `dev` + old lock delay | −52.25 dBFS | **bit-identical** | −183.77 dB | −25.68 dB |
| old | `dev` + old lock delay | −76.24 dBFS | −76.22 dBFS | −133.82 dB | −49.65 dB |

Reading the table:

1. Reverting `PilotPhaseLock` alone in `dev` removes the entire difference
   (row 2: −150 dBFS is the float noise floor of the comparison). Reverting it
   makes `dev` differ from `dev` by exactly the amount `old` differs from
   `dev` (row 3). **The pilot PLL accounts for 100 % of the difference.**
2. The phase-detector change is not part of it (row 4).
3. Restoring only the 0.5 s lock delay makes `dev` **bit-identical to itself
   after t = 0.5 s** (row 6) while removing the whole −52 dBFS: the entire
   headline difference is the startup transient.
4. What is left after removing the lock delay (row 7, −76.24 dBFS) is the loop
   retune, and it is confined to S: M still matches at −133.82 dB.
5. That M residual of −133.82 dB is present in *every* row that spans the two
   revisions and absent from the rows that do not. It is the FIR low-pass
   rewrite (`doc/LPF_VOLK_20260725.md`) plus the multipath filter's sixth-digit
   noise, 116 dB below the mono signal.

## 5. The startup transient

`PilotPhaseLock` counts IF samples at 384 kHz before declaring lock:

| build | expression | samples | time |
|---|---|---|---|
| `20260716-0` | `15.0 / bandwidth` | 192000 | **0.500 s** |
| `dev` | `6.0 / bandwidth_pll` | 76800 | **0.200 s** |

Until `locked()` is true, `FmDecoder::process()` takes the
`mono_to_left_right(m_buf_mono, audio)` branch, which duplicates the mono
signal into both channels, so S is identically zero. Mid/side levels across the
transition make this literal:

| window | old M | old S | new M | new S |
|---|---|---|---|---|
| 0.00–0.20 s | −16.57 | 0 exactly | −16.57 | −43.16 |
| 0.20–0.50 s | −18.07 | −52.57 | −18.07 | **−24.04** |
| 0.50–0.70 s | −17.87 | −29.72 | −17.87 | −29.72 |
| 1.00–2.00 s | −16.76 | −24.24 | −16.76 | −24.24 |
| 10–20 s | −17.05 | −22.65 | −17.05 | −22.65 |

(dBFS; steady-state S is about −22.7 dBFS.)

Per 10 ms frame, the difference between the builds is −150 dBFS until t = 0.20 s,
jumps to −22…−26 dBFS for exactly the 0.20–0.50 s window, and decays back
below −100 dBFS by t = 0.70 s. Energy shares of the whole-file difference:

| window | share of difference energy |
|---|---|
| 0–1 s | **99.636 %** |
| 51.5–53.5 s | 0.349 % |
| 129.5–131.5 s | 0.015 % |
| everything else (t > 2 s) | 0.0000 %, at −128.22 dBFS |

This is not a defect in either build. `dev` produces correct stereo 0.3 s
earlier; `20260716-0` produces correct mono for 0.3 s longer. Both are decoding
the same broadcast correctly by t = 0.7 s. It is, however, exactly the kind of
difference that dominates an A/B listening comparison started from the top of a
file, and it dominates any whole-file difference metric by 24 dB.

## 6. The two residual bursts

With the lock delay held constant, the loop retune (phasor biquad widened from
~34/160 Hz to ~40/188 Hz, PI gains rescaled ×0.889, ζ 0.57 → 0.71 — see
`doc/PLL_REDESIGN_20260723.md`) leaves −76.2 dBFS, and 91 % of *that* is two
short bursts:

| time | peak | duration (within 20 dB of peak) | level relative to program |
|---|---|---|---|
| 52.18 s | −39.0 dBFS | 70 ms | −24.0 dB |
| 130.40 s | −52.5 dBFS | 60 ms | −38.7 dB |

Neither coincides with a pilot fade. The pilot level, averaged and logged every
20 blocks, stays at 0.0955 within ±0.005 for the whole run apart from the initial
acquisition and a shallow dip near 95 s, and the stereo state machine printed
`got stereo signal` once and never printed `lost stereo signal`. So these are
not lock/unlock events; they are the two loops responding differently to a
brief disturbance in the pilot, which is what a damping change is expected to
produce. Outside those two windows the two loops track to −128 dBFS.

## 7. What this does and does not establish

- It establishes that on **this** recording, at `-E 36`, the multipath filter
  contributes nothing to the old-versus-new difference. It converges to the
  same coefficients, never diverges, and the difference survives switching it
  off.
- It does **not** clear the alpha order-scaling in general. That change is a
  bit-exact no-op only because `-E 36` lands exactly on
  `alpha_reference_order = 145`. At any other `-E` value it changes the
  adaptation rate, and this measurement says nothing about that case. The field
  evaluation caveat in `doc/MULTIPATH_FILTER_DESIGN_20260724.md` still stands.
- It does not evaluate audio *quality*. Within the 15 kHz audio bandwidth both
  builds show the same band levels in M (to 0.00 dB) and in S (to 0.02 dB), the
  same crest-factor statistics, and the same count of impulsive frames (2 of
  19,999, max crest 21.55 dB in both), so no metric here favors either one. Per
  `doc/MULTIPATH_FILTER_DESIGN_20260724.md` §17.4, none of these are a
  substitute for listening.
- For scale, what the multipath filter itself does to this recording, measured
  in the old build by decoding with and without `-E 36`:

  | quantity | value |
  |---|---|
  | RMS(with − without) | −22.58 dBFS |
  | M difference relative to M | −5.73 dB |
  | S difference relative to S | −4.22 dB |

  The filter changes the audio by an amount only 5.7 dB below the mono signal.
  The steady-state difference between the two builds is −128 dBFS. The build
  change is about 105 dB smaller than the filter's own effect, and 30 dB
  smaller even counting the startup transient.

## 8. Reproduction

Build the old revision in a throwaway worktree, and one hybrid to confirm the
attribution:

```sh
git worktree add --detach /tmp/old-20260716 20260716-0
git worktree add --detach /tmp/hyb-pll dev
(cd /tmp/hyb-pll && git checkout 20260716-0 -- \
    include/PilotPhaseLock.h sfmbase/PilotPhaseLock.cpp include/Utility.h)
# both worktrees need r8brain-free-src populated
for d in /tmp/old-20260716 /tmp/hyb-pll; do
  cmake -S $d -B $d/build && cmake --build $d/build --target all
done
```

Decode with each binary using the command in §1, then compare. The two numbers
that settle the question are the whole-file difference and the same figure with
the first half second discarded:

```python
import numpy as np, soundfile as sf
a, sr = sf.read("old.wav", dtype="float64", always_2d=True)
b, _  = sf.read("new.wav", dtype="float64", always_2d=True)
n = min(len(a), len(b)); a, b = a[:n], b[:n]
db = lambda v: 20 * np.log10(np.sqrt(np.mean(v ** 2)))
for skip in (0.0, 0.5):
    i = int(skip * sr)
    d = a[i:] - b[i:]
    M = (a[i:, 0] + a[i:, 1]) / 2 - (b[i:, 0] + b[i:, 1]) / 2
    S = (a[i:, 0] - a[i:, 1]) / 2 - (b[i:, 0] - b[i:, 1]) / 2
    print(f"skip {skip}: L/R {db(d):.2f} dBFS  M {db(M):.2f}  S {db(S):.2f}")
```

For the coefficient side, build both revisions with
`-DEXTRA_FLAGS="-DCOEFF_MONITOR=1"` and compare the `mf_coeff` lines at
matching block numbers; the tap vectors should agree to about −100 dB, and
`MF_RESET` should never appear.

Note that the old build's `COEFF_MONITOR` block sits inside the
`if (!quietmode)` branch, so the old binary must be run **without** `-q` to
produce dumps; on `dev` that was fixed in `b6b7d40`
(`doc/MF_DEBUG_CODE_20260726.md` §7).
