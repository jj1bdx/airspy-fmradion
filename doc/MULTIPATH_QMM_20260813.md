# Listening quality of the QMM output at multipath filter waits of 20 and 50 blocks (2026-08-13)

Follow-up to `doc/MULTIPATH_WAITING_20260812.md`, which set
`FmDecoder::m_wait_multipath_blocks` to 20 (commit `de9dc0c`). That evaluation
judged the decoder by its **normal stereo output**, where the only available
figure of merit is the difference from a reference decode, because the correct
output is unknown. This report re-runs the same grid through the **Quadrature
Multipath Monitor** output (`-X`), where the correct output *is* known — it is
zero — so the absolute level of the output is itself the quality figure.

**This is an evaluation only. No tracked source file was modified.** All
builds come from a throwaway `git worktree` checkout (`/tmp/mfqmm`) at
`de9dc0c`; `git status` in the main tree is unchanged by this work.

---

## Executive summary

- **Keep the wait at 20 blocks. On the QMM measure, 20 beats 50 in every one
  of the 21 (recording × stage-count) cells**: whole-file quadrature residual
  is 0.08–13.80 dB lower (median 1.50 dB), and 0.21–8.71 dB lower than at the
  old wait of 100 (median 2.12 dB). There is no cell in which 50 is
  better (§6).
- **The whole effect lives in the first second.** After 1 s the QMM residual
  at w20 and w50 agrees to within 0.25 dB, and across w20/w50/w100 to within
  0.93 dB (§5). A receiver left running cannot usefully tell them apart. This
  is a startup-quality result, not a steady-state one.
- **The mechanism is a mute window, and it is exact.** QMM output is digital
  zero until the pilot PLL declares lock, which happens at a fixed
  76800 IF samples = **0.2000 s** (`m_lock_delay = 6.0 / bandwidth_pll`,
  `sfmbase/PilotPhaseLock.cpp:45`). At 2048-sample blocks a 20-block wait
  engages the multipath filter at 0.107 s — 88 ms *inside* the mute — so its
  engagement transient has largely decayed before any of it can be emitted. A
  50-block wait engages at 0.267 s, after the output has opened, and the
  transient is fully audible (§7).
- **The worst possible wait is the one that lands engagement on the lock
  instant.** A controlled bracket at waits 30/35/37/38/40/45 puts the peak at
  **w = 37** (0.197 s, the lock instant to within one block), 42.5 dB above the
  run's own noise floor on `piano_iqtest.wav`. w50 sits on the shoulder of
  that peak; w20 is 19.7 dB below it (§8).
- **Startup burst, w20 versus w50: lower in 21/21 cells**, median 13.2 dB,
  range 4.2–21.7 dB. Versus w100: lower in 21/21, median 9.1 dB (§7).
- **The QMM output settles to its own floor sooner with a shorter wait**, in a
  clean monotone ordering w20 ≤ w50 ≤ w100 across nearly the whole grid — for
  example 0.24 / 0.54 / 0.68 s on joakfm at `-E36` (§7.3).
- **`-X` perturbs nothing upstream, and this is measured, not assumed.** For
  all 84 cells the entire instrumentation stream (IF AGC gain per block,
  per-block peak filter output and CMA error, divergence resets, coefficient
  dumps) is **byte-identical** to the corresponding non-`-X` decode of
  `MULTIPATH_WAITING_20260812.md`, as are all 12 pilot-PLL frequency traces
  (§4.3).
- **A no-wait anchor confirms the earlier verdict on an absolute scale.** At
  `-E100` with no wait, the steady QMM residual rises by 17.5 dB (airspy),
  25.9 dB (interfm) and 14.2 dB (piano), reaching **1.9 dB below the program's
  own L−R level** on interfm — and the pilot loses lock for 2.84% of that run,
  which `-X` reports directly as digital silence (§9).

---

## 1. What the QMM output is, and why its absolute level is the metric

`-X` / `--pilotshift` (`main.cpp:379`, `393`, `464-465`) sets exactly one
boolean, `FmDecoder::m_pilot_shift` (`include/FmDecode.h:129`, initialized at
`sfmbase/FmDecode.cpp:32` from the constructor argument passed at
`main.cpp:868`). Nothing else in the program sets it, and only four sites in
`FmDecoder::process()` read it.

Inside the pilot PLL the flag is read in exactly one place,
`sfmbase/PilotPhaseLock.cpp:91-99`:

```cpp
if (pilot_shift) {
  samples_out[i] = 2 * pcos * pcos - 1;   // cos(2*phase)
} else {
  samples_out[i] = 2 * psin * pcos;       // sin(2*phase)
}
```

`demod_stereo()` (`sfmbase/FmDecode.cpp:229-244`) then multiplies the MPX
baseband by that regenerated 38 kHz carrier. With `sin(2*phase)` this is the
ordinary coherent recovery of L−R. With `cos(2*phase)` it recovers the
**quadrature** component of the 38 kHz DSB-SC subcarrier — the component that
an ideal transmission through an ideal channel leaves empty.

`doc/QMM-noise-filter.md` states the design intent directly:

> * Q must be zero for an ideal transmission
> * Reality: Q is not zero, and represents multipath and other non-linear
>   distortion of S (L-R signal)

with the stated strategy "minimize Q^2". The idea is Brian Beezley K6STI's
(`README.md:253`).

That is why this report is not a repeat of the earlier one with a different
flag. `MULTIPATH_WAITING_20260812.md` could only ask *"how far is this decode
from the 100-block decode?"* — a relative question whose answer is smallest
for whatever setting the baseline happens to use. QMM asks *"how much
quadrature distortion is there?"*, and the ideal answer is known and identical
for every setting: zero, less is better. The two questions do not have the
same answer, and §6 is where they diverge.

### 1.1 Three code facts that shape the metrics

All in `sfmbase/FmDecode.cpp`:

- **Both PCM channels carry the same float.** Under `-X` and pilot lock the
  output goes through `mono_to_left_right()` (line 208, body 247-257), which
  writes `audio[2*i] = audio[2*i+1] = m`. L and R are bit-identical by
  construction, so a mid/side split of the difference is structurally
  degenerate — S is identically zero in *every* QMM decode, and "S difference"
  is `0 − 0`, not a measurement. Stereo separation is likewise a foregone
  conclusion. §14.2 of the earlier report used exactly that M/S split as its
  diagnostic; **it cannot be carried over**, and is replaced here by the
  absolute-level analysis. All levels below are computed on channel 0, and the
  analysis verifies `L == R` exactly rather than assuming it — it holds in all
  84 decodes.
- **De-emphasis is skipped on the QMM path** (lines 172-175,
  `if (!m_pilot_shift)`), while the 19 kHz pilot-cut filter (line 201) and the
  DC blocker (line 203) still run. So Q is measured flat while the normal-mode
  L−R is measured de-emphasized; the Q/S ratios in §5.1 are listening figures,
  not like-for-like spectral ratios.
- **Unlocked output is exact digital zero.** When `m_stereo_detected` is
  false, `-X` emits `zero_to_left_right()` (line 216, body 279-288) where
  normal mode falls back to duplicated mono (line 219). This makes the time of
  the first non-zero sample a direct readout of stereo lock, makes the
  zero-sample fraction a lock-loss counter — and, as §7 shows, is the whole
  reason a 20-block wait wins.

---

## 2. The mute window, in exact numbers

`PilotPhaseLock::locked()` returns `m_lock_cnt >= m_lock_delay`
(`include/PilotPhaseLock.h:63`), where

```cpp
m_lock_delay(static_cast<unsigned int>(6.0 / bandwidth_pll))   // .cpp:45
static constexpr double bandwidth_pll = 30 / sample_rate_if;   // .h:35
```

so `m_lock_delay` = 6.0 × 384000 / 30 = **76800 samples** of the 384 kHz
baseband = **0.2000 s**, and `m_lock_cnt` accumulates the baseband block size
each block (`.cpp:168-169`). The source comment at `.cpp:44` says as much:
"Lock decision (settling) time: 0.2 second (for 30Hz bandwidth)".

The wait, by contrast, is a **block** count at the source's native block size,
so whether the multipath filter engages inside or outside the mute window is
device-dependent. Engagement is inside the window iff

    wait * if_blocksize / ifrate  <  0.2 s

| device | `if_blocksize` / native rate | block period | boundary wait | wait = 20 |
| --- | --- | --- | --- | --- |
| `FileSource` (this study), Airspy HF+ | 2048 / 384000 | 5.333 ms | 37.5 blocks | 0.107 s — **inside** |
| Airspy R2 / Mini | 65536 / 10,000,000 | 6.554 ms | 30.5 blocks | 0.131 s — **inside** |
| RTL-SDR (default 1,152,000) | 16384 / 1,152,000 | 14.22 ms | 14.1 blocks | 0.284 s — **outside** |

Measured lock time was **0.195 s in all 84 decodes**, independent of wait and
stage count — 0.2000 s of IF time less the audio resampler's group delay. The
constancy is itself a useful check: the wait does not move the lock instant.

The "boundary wait" column is the point at which engagement stops preceding
the unmute at all; it is **not** a threshold below which the transient is
safely hidden. The transient needs time to decay, so what matters is how far
*before* the unmute the filter engages. §8 measures that decay directly: a
wait of 37 blocks is nominally inside the window by 3 ms and is nonetheless
the worst setting tested.

---

## 3. What `-X` does not change

Everything ahead of `m_pilotpll.process()` (`sfmbase/FmDecode.cpp:162`) is
computed with no reference to `m_pilot_shift`: the IF filter (line 101), the
IF AGC (line 107), the multipath filter with its wait countdown and divergence
guard (lines 109-134), and the phase discriminator (line 137).

Within the PLL, the flag touches only `samples_out[i]`. The phasor pair the
loop actually runs on is formed from `psin`/`pcos` and the input sample, not
from `samples_out` (`PilotPhaseLock.cpp:103-104`), and no biquad state, loop
filter, frequency accumulator, phase accumulator, pilot-level estimate, lock
counter or PPS update reads it (lines 107-180).

So `-X` is a pure output selector. §4.3 measures this rather than trusting it.

---

## 4. Method

### 4.1 Builds

One throwaway `git worktree` at `de9dc0c`, two build directories:

| variant | extra flags | purpose |
| --- | --- | --- |
| `build-dbg` | `COEFF_MONITOR DEBUG_MULTIPATH_FILTER DEBUG_MF_RESET DEBUG_MF_ERR DEBUG_AGC_TRACE` | the whole grid |
| `build-pll` | `DEBUG_PLL_FILTER` | pilot-PLL frequency traces |

The wait is settable at run time from the `MF_WAIT_BLOCKS` environment
variable, reusing the round-2 harness of `MULTIPATH_WAITING_20260812.md` §13.1
verbatim (`+103/−8` in `sfmbase/FmDecode.cpp`, worktree only) with its default
updated from 100 to 20 to track `de9dc0c`. Every run echoes `MFWAIT,<n>` to
stderr and the check in §4.3 verifies that echo against the value the job was
supposed to use, for all 84 cells.

Environment: Apple M2 Pro, macOS 25.6.0 (Darwin), Homebrew clang,
`-O3 -ftree-vectorize -std=c++20`, VOLK 3.3.0. No `-ffast-math`, per
`CLAUDE.md`.

**Continuity with the previous report was established before anything else
was believed.** Decoding `piano_iqtest.wav` at `-E36`, `MF_WAIT_BLOCKS=20`,
*without* `-X` through this new build reproduces the corresponding round-2
output file **bit-exactly** (`cmp`, not a tolerance). The two reports are
therefore measuring the same decoder.

### 4.2 Coverage

| axis | values |
| --- | --- |
| recordings | all 7 of `test-files/`, per §1.2 of the previous report |
| stage counts | `-E18`, `-E36`, `-E100` |
| wait values | 20 and 50 (the question), plus 100 (baseline) and 0 (scale anchor) |
| main grid | 7 × 3 × 4 = **84 decodes**, all `-X`, all `EXIT:0` |
| pilot-PLL traces | airspy `-E36`/`-E100`, interfm `-E100`, × 4 waits = **12 decodes** |
| boundary bracket (§8) | piano + joakfm `-E36` at waits 30/35/37/38/40/45 = **12 decodes** |

108 decodes, 7,760 s of real-time-paced material. Waits 0 and 100 are not part
of the question; 100 anchors the comparison to the previous report and 0
supplies the known-bad reference that gives the small w20/w50 margins a scale.

Each recording is decoded independently at every grid point, so the runs were
executed 12-way concurrent (`xargs -P 12`); the main grid finished in 10 min
22 s. Concurrency is legitimate here for the reason established in §13.2 of the
previous report — nothing in the DSP path is wall-clock derived, and the one
timing hazard, `DataBuffer`'s bounded drop-oldest queue, announces itself.

### 4.3 Validity, and the upstream-invariance test

| check | result |
| --- | --- |
| `EXIT:0` | 84/84 (plus 12/12 PLL, 12/12 bracket) |
| `DataBuffer: queue overflow` | 0 logs |
| `MFWAIT,<n>` echo matches the intended wait | 84/84 |
| `L == R` exactly in the output | 84/84 |
| divergence resets across the whole grid | 0 |

And the test that makes the rest of the report interpretable:

> For **all 84 cells**, the SHA-256 of the complete instrumentation stream —
> every `AGCTRACE` line (IF AGC gain per block), every `MFERR` line (per-block
> peak filter output and CMA error), every `MFRESET` line, and every
> `COEFF_MONITOR` coefficient dump — is **identical** to that of the
> corresponding non-`-X` decode from `MULTIPATH_WAITING_20260812.md`.
>
> For all **12** `DEBUG_PLL_FILTER` traces (37,500 or 18,750 lines each), the
> per-block `m_freq` / `m_freq_err` / `m_pilot_level` stream is likewise
> byte-identical.

So every difference reported below arises downstream of the PLL loop, in the
choice of demodulating carrier phase alone. The multipath filter, the AGC and
the PLL behaved identically in the two studies, sample for sample. This also
means the coefficient, reset and PLL-frequency analyses of the previous report
carry over unchanged and are not repeated here.

---

## 5. Steady state: the wait does not matter

QMM residual level after the first second, dBFS:

**`-E36`**

| file | w0 | w20 | w50 | w100 |
| --- | --- | --- | --- | --- |
| airspy | −54.37 | −54.41 | −54.40 | −54.34 |
| interfm | −52.53 | −52.53 | −52.53 | −52.52 |
| joakfm | −69.49 | −69.51 | −69.50 | −69.50 |
| piano | −70.06 | −70.42 | −70.41 | −70.38 |
| pa0p5 | −64.79 | −64.73 | −64.56 | −64.20 |
| pa0p9 | −56.05 | −55.98 | −55.73 | −55.23 |
| pa1p2 | −51.96 | −51.93 | −51.94 | −51.59 |

**`-E100`**

| file | w0 | w20 | w50 | w100 |
| --- | --- | --- | --- | --- |
| airspy | **−36.22** | −53.86 | −53.83 | −53.74 |
| interfm | **−26.23** | −52.13 | −52.12 | −52.11 |
| joakfm | −69.51 | −69.79 | −69.78 | −69.77 |
| piano | **−56.43** | −70.66 | −70.63 | −70.58 |
| pa0p5 | −61.55 | −61.51 | −61.37 | −61.04 |
| pa0p9 | −53.13 | −53.07 | −52.86 | −52.49 |
| pa1p2 | −54.25 | −54.15 | −53.96 | −53.22 |

Setting the `-E100` w0 column aside (§9), **w20 is at or below the w100 level
in 20 of 21 cells and at or below w50 in 19 of 21**, but every margin falls
within −0.93 to +0.15 dB against w100 (median −0.07 dB) and within −0.25 to
+0.06 dB against w50 (median −0.01 dB). The honest reading is that the steady
state is wait-independent: once the filter has converged, when it started
does not measurably change how much quadrature distortion it leaves.

There is one weak systematic trend worth recording: on the three synthetic
two-ray files, whose channel is static and whose AGC needs no ramp, a longer
wait leaves a *slightly higher* steady residual (pa0p9 `-E36`: −56.05 at w0
rising monotonically to −55.23 at w100). It is 0.8 dB at most and does not
affect any conclusion, but it runs the opposite way to "longer wait is safer."

### 5.1 The same figures as a quadrature-to-signal ratio

QMM residual relative to the normal-mode L−R level of the same cell (dB;
whole file; the de-emphasis asymmetry of §1.1 applies), `-E36`:

| file | w0 | w20 | w50 | w100 |
| --- | --- | --- | --- | --- |
| airspy | −27.70 | −27.72 | −26.73 | −26.65 |
| interfm | −28.80 | −28.76 | −27.89 | −27.52 |
| joakfm | −37.59 | −38.48 | −35.52 | −34.40 |
| piano | −37.33 | **−43.14** | −29.33 | −34.42 |
| pa0p5 | −38.70 | −38.23 | −29.46 | −33.63 |
| pa0p9 | −29.75 | −29.73 | −26.71 | −27.64 |
| pa1p2 | −25.50 | −25.41 | −24.04 | −22.97 |

On the two off-air recordings the quadrature residual sits 26–29 dB below the
program's own L−R content, which is the ordinary state of affairs for
real multipath. On `pa1p2` — the non-minimum-phase two-ray case the filter
cannot invert — it is only 23–25 dB down, the worst of the set, and the wait
barely moves it.

---

## 6. Whole file: 20 beats 50 in every cell

Whole-file QMM residual, dBFS. This is the figure that includes the startup
second, and it is where the two waits separate:

**`-E18`**

| file | w0 | w20 | w50 | w100 |
| --- | --- | --- | --- | --- |
| airspy | −54.45 | **−54.42** | −53.52 | −53.35 |
| interfm | −52.58 | **−52.54** | −51.77 | −51.31 |
| joakfm | −68.45 | **−69.43** | −67.19 | −64.87 |
| piano | −64.37 | **−69.33** | −56.21 | −61.08 |
| pa0p5 | −64.71 | **−64.35** | −56.60 | −60.14 |
| pa0p9 | −56.18 | **−56.16** | −53.61 | −54.04 |
| pa1p2 | −42.26 | **−42.26** | −42.18 | −42.05 |

**`-E36`**

| file | w0 | w20 | w50 | w100 |
| --- | --- | --- | --- | --- |
| airspy | −54.27 | **−54.29** | −53.30 | −53.22 |
| interfm | −52.49 | **−52.46** | −51.59 | −51.22 |
| joakfm | −68.56 | **−69.45** | −66.49 | −65.37 |
| piano | −62.56 | **−68.35** | −54.55 | −59.63 |
| pa0p5 | −64.14 | **−63.67** | −54.92 | −59.11 |
| pa0p9 | −55.46 | **−55.44** | −52.44 | −53.36 |
| pa1p2 | −51.29 | **−51.20** | −49.88 | −48.85 |

**`-E100`**

| file | w0 | w20 | w50 | w100 |
| --- | --- | --- | --- | --- |
| airspy | −35.73 | **−53.71** | −53.06 | −52.71 |
| interfm | −26.14 | **−52.01** | −51.40 | −50.91 |
| joakfm | −67.83 | **−69.76** | −68.26 | −65.72 |
| piano | −47.00 | **−70.05** | −58.37 | −62.94 |
| pa0p5 | −60.93 | **−60.82** | −57.13 | −59.02 |
| pa0p9 | −52.51 | **−52.46** | −51.58 | −51.53 |
| pa1p2 | −52.60 | **−52.37** | −51.24 | −49.46 |

Head to head, over all 21 cells:

| comparison | w20 lower in | margin min | max | median |
| --- | --- | --- | --- | --- |
| w20 vs w50 | **21 / 21** | 0.08 dB | 13.80 dB | 1.50 dB |
| w20 vs w100 | **21 / 21** | 0.21 dB | 8.71 dB | 2.12 dB |

The largest margins are on `piano_iqtest.wav` (13.8 dB at `-E36`) and
`pa0p5` (8.8 dB) — the clean and mildly faded synthetic files, where the
steady QMM floor is low enough that a startup burst dominates the whole-file
figure. On the off-air recordings, whose floor is 15 dB higher, the same
burst only moves the whole-file number by 0.6–1.0 dB.

### 6.1 The relative metric of the previous report points the other way

Difference from the w100 QMM decode after 1 s, dBFS, `-E36`:

| file | w0 | w20 | w50 |
| --- | --- | --- | --- |
| airspy | −72.29 | −74.87 | **−77.57** |
| interfm | −78.20 | −83.63 | **−85.99** |
| joakfm | −88.24 | −104.63 | **−108.05** |
| piano | −83.61 | −94.94 | **−96.77** |
| pa0p5 | −77.75 | −77.95 | **−80.00** |
| pa0p9 | −69.99 | −70.99 | **−74.16** |
| pa1p2 | −65.32 | −65.67 | **−67.44** |

By this metric w50 wins everywhere, by 1.8–3.4 dB — exactly as §14 of the
previous report found for the stereo output ("going from 20 to 50 buys a
further 2–6 dB"). The metric is not wrong; it is answering a different
question. *Closeness to the 100-block decode* is maximized by being closer to
100, which is a statement about the baseline, not about quality. Once the
correct answer is known independently — Q = 0 — the ordering reverses. Whole-file
after-1 s correlation with w100 tells the same story (w20 0.979–0.9999,
w50 0.981–0.9999 across the grid) and is likewise a similarity measure, not a
quality measure.

This is the single most useful methodological result in this report: **on the
wait question, "difference from baseline" and "absolute distortion" disagree,
and only the second one is a listening-quality statement.**

---

## 7. The mechanism: the engagement transient falls inside the mute window

### 7.1 The startup trace

QMM output level in 20 ms windows, `piano_iqtest.wav` at `-E36`, dBFS.
`−3000` is the analysis floor for an all-zero window, i.e. output muted:

| t (s) | w0 | w20 | w50 | w100 |
| --- | --- | --- | --- | --- |
| 0.00–0.16 | −3000 | −3000 | −3000 | −3000 |
| 0.18 | −46.4 | −50.4 | −65.2 | −65.2 |
| 0.20 | −41.0 | −45.4 | −56.7 | −56.7 |
| 0.22 | −38.7 | −50.2 | −55.3 | −55.3 |
| 0.24 | −42.3 | −52.6 | −54.5 | −54.5 |
| 0.26 | −43.7 | −58.2 | **−29.7** | −55.0 |
| 0.28 | −50.2 | −62.8 | **−29.7** | −58.3 |
| 0.30 | −50.6 | −64.9 | **−31.7** | −58.6 |
| 0.32 | −44.9 | −63.5 | −34.4 | −52.7 |
| 0.34 | −46.2 | −62.9 | −37.7 | −53.7 |
| 0.36 | −48.7 | −64.7 | −42.8 | −53.1 |
| 0.38 | −50.6 | −65.9 | −47.5 | −53.5 |
| 0.40 | −53.7 | −67.8 | −53.0 | −56.7 |
| 0.44 | −57.4 | −68.9 | −62.0 | −59.4 |
| 0.48 | −61.8 | −70.4 | −69.2 | −60.5 |
| 0.52 | −65.8 | −71.1 | −70.6 | −50.8 |
| 0.54 | −65.8 | −71.2 | −70.7 | **−33.8** |
| 0.56 | −65.7 | −71.0 | −70.7 | **−34.8** |
| 0.58 | −62.3 | −70.3 | −69.6 | −38.3 |
| 0.62 | −60.0 | −69.9 | −69.1 | −49.2 |
| 0.68 | −59.3 | −68.6 | −67.9 | −58.3 |

Everything in the mechanism is visible in this one table:

- Output is exact zero until 0.18 s in every column: the mute window.
- w50 and w100 are **identical to each other** through 0.24 s — neither has
  engaged the filter yet — which is the shared-upstream property of §4.3
  showing up in the audio.
- w50 engages at 0.267 s and bursts to −29.7 dB, 40.7 dB above its own
  eventual floor, decaying back by ~0.44 s.
- w100 engages at 0.533 s and bursts to −33.8 dB, decaying by ~0.68 s.
- w20 engaged at 0.107 s, 88 ms *before* the output opened. Most of its
  transient decayed inside the mute; what remains at 0.18 s descends
  monotonically to the floor, reaching it by 0.42 s, with no burst at all.
- w0 is elevated and noisy throughout (−38 to −65 dB), worse than w20 at every
  point in the window — the unsettled-AGC penalty of the previous report,
  seen directly.

One detail of the table cuts against a tempting over-claim and is worth
stating plainly. In the 0.195–0.26 s window — output open, w50/w100 still
bypassing the filter — w20 is *not* uniformly better, because it is the only
run whose filter is adapting at that moment:

| file (`-E36`) | w20 | w50 = w100 | w20 − w50 |
| --- | --- | --- | --- |
| airspy | −38.5 | −32.6 | **−5.9** |
| interfm | −40.4 | −33.8 | **−6.6** |
| pa1p2 | −40.4 | −36.1 | **−4.2** |
| pa0p9 | −48.3 | −44.6 | **−3.7** |
| pa0p5 | −48.7 | −51.3 | +2.6 |
| joakfm | −58.4 | −60.7 | +2.4 |
| piano | −47.9 | −55.6 | +7.7 |

On the channels that actually carry distortion for the filter to cancel — the
two off-air recordings and the two stronger two-ray cases — a 20-block wait
delivers output that is already 3.7–6.6 dB cleaner the instant the listener
first hears it. On the near-clean cases (piano, joakfm, pa0p5) it is 2.4–7.7 dB
*worse* in that same window, because an adaptive filter with nothing to cancel
contributes only gradient noise. Both effects are small and short-lived next
to the startup peak w50 reaches shortly afterward, which runs 4.2–21.7 dB
higher than w20's (median 13.2 dB, §7.2). That is why the whole-file ordering
of §6 is unanimous even though "engage earlier, hear less" is not true window
by window, only in the aggregate.

### 7.2 Startup peak across the grid

Highest 50 ms window in the first 2 s, dBFS, with its height above that run's
own steady floor in parentheses:

**`-E36`**

| file | w0 | w20 | w50 | w100 |
| --- | --- | --- | --- | --- |
| airspy | −40.2 (+14.2) | **−38.1 (+16.3)** | −26.8 (+27.6) | −32.2 (+22.1) |
| interfm | −43.0 (+9.5) | **−40.3 (+12.3)** | −28.4 (+24.1) | −31.4 (+21.1) |
| joakfm | −51.8 (+17.7) | **−58.5 (+11.1)** | −40.1 (+29.4) | −39.7 (+29.8) |
| piano | −39.9 (+30.2) | **−47.6 (+22.8)** | −30.7 (+39.7) | −36.0 (+34.4) |
| pa0p5 | −52.3 (+12.5) | **−48.4 (+16.3)** | −31.5 (+33.1) | −37.6 (+26.6) |
| pa0p9 | −44.4 (+11.7) | **−44.5 (+11.5)** | −31.0 (+24.8) | −37.9 (+17.4) |
| pa1p2 | −39.7 (+12.3) | **−39.3 (+12.6)** | −31.9 (+20.0) | −30.3 (+21.2) |

**`-E100`**

| file | w0 | w20 | w50 | w100 |
| --- | --- | --- | --- | --- |
| airspy | −17.9 (+18.3) | **−38.0 (+15.9)** | −27.7 (+26.1) | −31.8 (+21.9) |
| interfm | −18.3 (+8.0) | **−39.1 (+13.1)** | −29.2 (+22.9) | −30.0 (+22.1) |
| joakfm | −48.4 (+21.1) | **−64.5 (+5.3)** | −43.4 (+26.4) | −37.6 (+32.1) |
| piano | −27.5 (+29.0) | **−54.8 (+15.9)** | −33.3 (+37.4) | −39.7 (+30.8) |
| pa0p5 | −49.0 (+12.5) | **−48.5 (+13.0)** | −34.2 (+27.2) | −40.9 (+20.2) |
| pa0p9 | −40.7 (+12.4) | **−40.7 (+12.4)** | −33.8 (+19.1) | −37.9 (+14.6) |
| pa1p2 | −38.8 (+15.4) | **−38.3 (+15.8)** | −34.2 (+19.8) | −30.3 (+22.9) |

Over all 21 cells, the startup peak is lower at w20 than at w50 in **21/21**
(median 13.2 dB, range 4.2–21.7 dB) and lower than at w100 in **21/21**
(median 9.1 dB, range 2.8–26.8 dB).

Note that some residual peak survives at w20: the 0.18–0.22 s windows carry
the pilot PLL's own post-lock settling, which is present at every wait — it is
visible in the w0 column, whose filter engaged 0.2 s earlier and which still
peaks at 0.20 s. What the mute window removes is the *filter engagement*
component on top of it, not the PLL's own acquisition.

### 7.3 Settling time of the QMM output

Last 20 ms window within the first 2 s that exceeds the run's own steady floor
by more than 6 dB (seconds):

**`-E36`**

| file | w0 | w20 | w50 | w100 |
| --- | --- | --- | --- | --- |
| airspy | 1.40 | **0.94** | 1.12 | 1.46 |
| interfm | 0.32 | 0.38 | 0.54 | 0.84 |
| joakfm | 1.10 | **0.24** | 0.54 | 0.68 |
| piano | 1.10 | **0.36** | 0.46 | 0.76 |
| pa0p5 | 0.78 | 0.78 | 1.10 | 1.10 |
| pa0p9 | 0.80 | 0.80 | 0.80 | 0.80 |
| pa1p2 | 0.78 | 0.78 | 0.78 | 0.82 |

**`-E100`**

| file | w0 | w20 | w50 | w100 |
| --- | --- | --- | --- | --- |
| airspy | 2.00 | **1.12** | 1.40 | 1.56 |
| interfm | 1.90 | **0.46** | 0.64 | 0.92 |
| joakfm | 1.30 | **0.54** | 0.88 | 1.28 |
| piano | 1.10 | **0.38** | 0.44 | 0.76 |
| pa0p5 | 0.78 | 0.78 | 1.10 | 1.10 |
| pa0p9 | 0.76 | 0.76 | 0.76 | 0.80 |
| pa1p2 | 1.10 | 1.10 | 1.10 | 1.10 |

The ordering w20 ≤ w50 ≤ w100 holds in every cell where the three differ at
all. The QMM output is usable sooner with the shorter wait, by up to 0.74 s
(joakfm `-E100`). The one exception to w20 being best outright is interfm at
`-E36`, where w0 settles 0.06 s sooner than w20 — a single 20 ms window.

---

## 8. Where the worst wait actually is

If the mute window is the mechanism, the worst wait should be the one that
lands engagement exactly on the lock instant — block 37.5 at this block size.
A bracket at waits 30/35/37/38/40/45, `-E36`, tests that directly (w20 and w50
from the main grid, for scale):

**`piano_iqtest.wav`** (steady floor −70.42 dBFS)

| wait | engagement | peak 50 ms | above floor |
| --- | --- | --- | --- |
| 20 | 0.107 s | −47.64 | +22.8 |
| 30 | 0.160 s | −34.28 | +36.1 |
| 35 | 0.187 s | −30.08 | +40.3 |
| **37** | **0.197 s** | **−27.93** | **+42.5** |
| 38 | 0.203 s | −28.49 | +41.9 |
| 40 | 0.213 s | −28.24 | +42.2 |
| 45 | 0.240 s | −29.81 | +40.6 |
| 50 | 0.267 s | −30.69 | +39.7 |

**`joakfm`** (steady floor −69.51 dBFS)

| wait | engagement | peak 50 ms | above floor |
| --- | --- | --- | --- |
| 20 | 0.107 s | −58.45 | +11.1 |
| 30 | 0.160 s | −44.45 | +25.1 |
| 35 | 0.187 s | −40.57 | +28.9 |
| **37** | **0.197 s** | **−38.39** | **+31.1** |
| 38 | 0.203 s | −40.19 | +29.3 |
| 40 | 0.213 s | −40.79 | +28.7 |
| 45 | 0.240 s | −39.18 | +30.3 |
| 50 | 0.267 s | −40.06 | +29.5 |

The prediction holds, and the transition is **graded, not a step**. The peak
climbs monotonically from w20 through w30 and w35, maxes at w37 — the lock
instant to within one block — and then flattens out 1–3 dB below the maximum
for every longer wait tested. The gradation is the transient's own decay: the
earlier the engagement, the more of the burst has died away before the output
opens, and only a 20-block wait is early enough for essentially all of it.

The practical consequence is that **50 is not merely worse than 20, it sits on
the shoulder of the worst available setting**, 17.0 dB (piano) and 18.4 dB
(joakfm) above the w20 peak, while w37 — the maximum — is 19.7 dB and 20.1 dB
above it.

---

## 9. The no-wait anchor, in QMM terms

Waits of 0 and 100 are not the question, but w0 supplies the scale. At
`-E100`, no wait:

| file | steady QMM, w0 | steady QMM, w100 | penalty | Q/S, w0 |
| --- | --- | --- | --- | --- |
| airspy | −36.22 | −53.74 | **+17.5 dB** | −9.01 dB |
| interfm | −26.23 | −52.11 | **+25.9 dB** | **−1.89 dB** |
| piano | −56.43 | −70.58 | **+14.2 dB** | −21.56 dB |

On interfm the quadrature distortion ends up **1.9 dB below the program's own
L−R level** — not a transient, a permanent state, since these are after-1 s
figures. After-1 s correlation with the w100 decode collapses to −0.005
(airspy), 0.017 (interfm) and −0.004 (piano): the residual is not a scaled
version of the correct one, it is unrelated to it.

`-X` also reports the lock failure directly, because unlocked output is
digital silence. Zero-sample fraction beyond the initial 0.195 s mute:

| cell | extra silence |
| --- | --- |
| interfm `-E100` w0 | **2.84%** of the run |
| airspy `-E100` w0 | 0.30% of the run |
| every other cell of the 84 | 0.00% |

This is an independent confirmation of the previous report's `-E100` finding,
arrived at through a different output path and a different metric — and the
lock loss is a new observation, visible here only because QMM mutes on unlock
where normal stereo silently falls back to mono.

---

## 10. What this establishes, and what it does not

Established:

- On the QMM measure, a 20-block wait is better than a 50-block wait in all 21
  cells, and the margin comes entirely from the first second.
- The mechanism is identified, quantified, and confirmed by a controlled
  bracket: how much of the filter's engagement transient reaches the output
  depends on how long before the 0.2 s unmute the filter engages, rising
  smoothly from w20 to a maximum at w37 and staying near that maximum for
  every longer wait.
- `-X` is a pure output selector; the two studies decoded identically upstream,
  byte for byte.

Not established:

- **Anything about steady-state listening quality.** After 1 s the choice
  between 20 and 50 is worth ≤0.25 dB of quadrature residual. If a listener's
  complaint is about sustained multipath, this parameter is not the lever.
- **Hardware sources.** Every decode is `filesource` at 384 kHz with
  2048-sample blocks. The mute-window arithmetic of §2 predicts the same
  benefit on Airspy HF+ (identical block size) and Airspy R2/Mini (20 blocks =
  0.131 s, still inside the 0.2 s window), but **not** on RTL-SDR at its
  default 1.152 MHz, where 20 blocks is 0.284 s and engagement is already
  outside the window. On that device w20 should still settle earliest but its
  transient would be audible. This is untested.
- **Whether the mute is the right behavior.** This report treats
  `zero_to_left_right()` on unlock as a fixed property of the program and
  measures against it. Whether QMM output *should* be silent before lock, and
  whether the burst would be better handled by ramping the filter in, are
  separate questions.
- **Any wait below 20.** The onset bracket of the previous report (§17 there)
  found the failure onset between 5 and 10 blocks, and nothing here revisits
  it. §8's trend — earlier engagement, smaller emitted burst — would keep
  improving below 20, but that region is where the filter misconverges, and
  the QMM measure is silent about misconvergence risk.

---

## 11. Recommendation

**No change. `m_wait_multipath_blocks = 20`, as shipped in `de9dc0c`, is
correct on the QMM measure as well.**

The QMM evaluation reaches the same verdict as the stereo-audio evaluation but
for an independent reason, which is the useful part. The earlier report chose
20 because it was the shortest wait that still avoided misconvergence, and
noted that its engagement "lands at 0.107 s, inside the PLL's own acquisition
transient." This report shows that placement is worth more than was known at
the time: 0.107 s is inside the **output mute window**, so the engagement
transient is not merely small, it is never emitted at all. By the time the QMM
output opens at 0.2 s the canceller has been running for 88 ms, which on the
recordings carrying real multipath means the first sound the listener hears is
already 3.7–6.6 dB cleaner (§7.1).

Had the choice gone the other way, to 50, the QMM output would have carried a
burst 19–40 dB above its own noise floor at 0.27 s on every start-up, on every
recording, at every stage count.

One thing this does change: the wait is now known to interact with the pilot
lock delay, and the interaction is one-sided. Every block added to the wait
between 20 and 37 moves the engagement transient further into the audible
region, and nothing beyond 37 wins any of it back. Any future proposal to
lengthen the wait should be measured against that cost, and 0.2 s of source
time — 37 blocks at 2048 samples, 30 on Airspy R2, 14 on a default RTL-SDR —
is where the cost is fully incurred.

---

## 12. Reproduction

```sh
git worktree add --detach /tmp/mfqmm dev
cp -R r8brain-free-src /tmp/mfqmm/ && rm -f /tmp/mfqmm/r8brain-free-src/.git
# Apply the round-2 harness of doc/MULTIPATH_WAITING_20260812.md section 19 to
# /tmp/mfqmm/sfmbase/FmDecode.cpp: initialize m_wait_multipath_blocks from a
# once-parsed getenv("MF_WAIT_BLOCKS") (default 20 at de9dc0c), echo MFWAIT,<n>
# to stderr, and add the DEBUG_MF_RESET / DEBUG_MF_ERR / DEBUG_AGC_TRACE prints.

cmake -S /tmp/mfqmm -B /tmp/mfqmm/build-dbg \
  -DEXTRA_FLAGS="-DCOEFF_MONITOR=1 -DDEBUG_MULTIPATH_FILTER=1 -DDEBUG_MF_RESET=1 -DDEBUG_MF_ERR=1 -DDEBUG_AGC_TRACE=1"
cmake -S /tmp/mfqmm -B /tmp/mfqmm/build-pll -DEXTRA_FLAGS="-DDEBUG_PLL_FILTER=1"
cmake --build /tmp/mfqmm/build-dbg --target airspy-fmradion -j
cmake --build /tmp/mfqmm/build-pll --target airspy-fmradion -j
```

Each decode, per the file/frequency table of `MULTIPATH_WAITING_20260812.md`
§1.2 — identical to that report's command except for `-X`:

```sh
MF_WAIT_BLOCKS=<0|20|50|100> /tmp/mfqmm/build-dbg/airspy-fmradion \
  -m fm -t filesource -E <18|36|100> -X \
  -c freq=<freq>,srate=384000,filename=test-files/<file>,wav,format=FLOAT \
  -q -G out/<tag>_w<wait>_e<stages>.wav 2> logs/<tag>_w<wait>_e<stages>.log
```

Run 12-way concurrent (`xargs -P 12`), longest recordings first, then verify
before trusting any of it:

```sh
grep -l "queue overflow" logs/*.log | wc -l    # must be 0
grep -h "^EXIT:" logs/*.log | sort | uniq -c   # must be all EXIT:0
grep -h "^MFWAIT," logs/<tag>_w<wait>_e<st>.log  # must echo <wait>
```

Continuity and invariance checks:

```sh
# same decoder as the previous report (no -X, wait 20, piano, -E36)
cmp qmm/chk_nox.wav r2/out/piano_w20_e36.wav

# -X changes nothing upstream: instrumentation streams must match, all 84 cells
for f in logs/*.log; do
  grep -hE '^(AGCTRACE|MFERR|MFRESET|block),' "$f"      | shasum
  grep -hE '^(AGCTRACE|MFERR|MFRESET|block),' "../r2/$f" | shasum
done
```

Metrics are computed on channel 0 only (§1.1): absolute level whole /
after-1 s / windowed, quadrature-to-signal ratio against the round-2
non-`-X` decode of the same cell, difference and correlation against the w100
QMM decode, peak 50 ms window in the first 2 s, settling time to within 6 dB
of the run's own floor, first-non-zero time and zero-sample fraction.
