# Should the multipath filter's 100-block warm-up wait be removed? (2026-08-12)

Evaluation of `FmDecoder::m_wait_multipath_blocks`, initialized to 100 in the
`FmDecoder` constructor (`sfmbase/FmDecode.cpp`) and decremented once per
`process()` call. While it is non-zero the multipath filter's `process()` is
never invoked — the AGC output passes straight through — so the filter's
delay line stays empty and its coefficients stay at the identity for the
whole wait. This report measures what changes if the wait is set to zero, so
the filter engages on the very first block.

**This is an evaluation only. No tracked source file was modified.** All
builds were produced from a throwaway `git worktree` checkout with a
compile-time macro added; `git status` in the main tree is unchanged by this
work.

**Structure.** Part I (§1–§12) tests the two endpoints, 0 and 100 blocks, and
concludes that the wait must be kept. Part II (§13–§19) tests the intermediate
values 20 and 50 across the same grid, plus an onset bracket at 5/10/15, and
reaches a different and stronger conclusion: **a wait of 20 blocks dominates
both endpoints.** §18 supersedes §11. Read the Part II summary below alongside
the Part I one — where they disagree, Part II has the wider evidence.

---

## Executive summary — Part II (the operative conclusion)

- **Set the wait to 20 blocks.** A 20-block wait is at the 100-block baseline
  on every metric, on all seven recordings and all three stage counts: audio
  within −73 to −107 dBFS after the first second, coefficient distance −26 to
  −68 dB with off-reference tap energy within 3% of baseline, zero divergence
  resets, and zero pilot-PLL limiter railing (§14–§16).
- **It removes the `-E100` failure that made removal unsafe.** At `-E100` the
  after-1 s audio difference improves by 57.7 dB on the airspy recording
  (−15.48 → −73.21 dBFS) and 77.7 dB on piano, and the whole stage-count
  dependence that drives Part I's verdict disappears (§14).
- **It keeps most of the benefit of removal.** The engagement kick shrinks
  (+18.1 Hz vs +19.9 Hz at `-E36`; +5.3 Hz vs +7.2 Hz at `-E100`) and lands at
  0.107 s, inside the PLL's own acquisition transient, where the 1–2 s
  frequency jitter is actually *lower* than with the 100-block wait (§16).
- **The failure onset is between 5 and 10 blocks**, bracketed directly. A
  5-block wait still misconverges as badly as no wait at all on the worst
  recording; 20 carries a 4× margin over it (§17).
- **A new worst case appeared**: `piano_iqtest.wav` at `-E100` with no wait
  decodes to audio essentially *uncorrelated* with the correct output
  (correlation **−0.235**, difference 4 dB above the signal, 194× the baseline
  off-reference tap energy) — worse than either off-air recording. It also
  refines Part I's §8 mechanism: the AGC gain ramp is **necessary but not
  sufficient**, since joakfm has an identical ramp and survives (§17).
- **Part I's `-E18`/`-E36` "no lasting difference" was an audio-domain
  statement.** In the tap domain, removing the wait leaves a permanent ~33%
  excess of off-reference energy even at those stage counts (§15.1).

---

## Executive summary — Part I (endpoints only; superseded where it conflicts)

- **At the shipping settings (`-E18`–`-E36`), removing the wait is safe and
  arguably an improvement.** The audio converges to the same steady state
  within about a second on every one of the seven test recordings, no run
  ever trips the divergence guard, and the well-documented +22 Hz pilot-PLL
  kick at t = 0.533 s (`doc/MF_PLL_DIFFERENCE_20260728.md` §6) essentially
  disappears — because the PLL now acquires lock and the multipath filter
  now engages at the same instant, instead of the filter blindsiding an
  already-locked loop with a step (§4, §7, §10).
- **At large stage counts (`-E100`), removing the wait is not safe to do
  unconditionally.** On both real off-air recordings tested, the `-E100`
  filter with the wait removed settles into a **persistently more dispersed
  coefficient state that has not reconverged to the waited baseline by the
  end of the recording** (100 s and 200 s respectively) — off-reference tap
  energy 5–6× the baseline and climbing for the whole run, whole-file audio
  correlation between the two variants falling to 0.28–0.89, and a
  post-1-second audio difference *at* or *above* the level of the signal
  itself (§5, §8, §9). The pilot PLL is measurably disturbed for about the
  first 10 seconds — repeatedly railing its ±30 Hz frequency limiter for
  12–16 % of blocks — before its own jitter recovers to baseline, even
  though the filter's tap state has not (§10).
- **The mechanism is identified, not just observed.** A targeted test on a
  synthetic two-ray file whose amplitude is already near the AGC's unity
  target (so there is nothing for the AGC to ramp) shows that a 401-tap
  (`-E100`) filter with the wait removed converges *cleanly* — same
  post-1-second audio floor as `-E18`/`-E36`, correlation 0.9935, no
  persistent coefficient offset. The pathology is therefore an **interaction
  between the AGC's initial gain ramp and a large stage count**, not a
  property of the multipath filter's cold delay line by itself (§8).
- **The divergence guard never fires**, in any of 26 decodes across 7
  recordings and 3 stage counts. Worst-case peak filter output with the wait
  removed is 2.80 against a `divergence_limit` of 10 — 11.1 dB of headroom,
  down from 16.9 dB with the wait in place, but still comfortable (§6).
- **The wait is not calibrated to the AGC.** The IF AGC reaches 98–100 % of
  its quasi-steady gain within about 20 blocks (0.107 s) on every recording
  that needs real gain correction, roughly a fifth of the 100-block wait
  (§3). The 100-block constant looks like a round number chosen with a
  margin, not a value derived from the AGC's own time constant.
- **The wait's duration is not what the constant suggests.** It is a block
  count, and block size and native sample rate both vary by device: 0.533 s
  for a file source or Airspy HF+, 0.655 s for an Airspy R2/Mini at its
  default 10 Msps, and 1.42 s for an RTL-SDR at its default 1.152 Msps —
  ranging up to 1.82 s at the RTL-SDR's lowest supported rate (§2).
- **Recommendation: do not remove the wait unconditionally.** Its cost at
  the common settings is negative (it *creates* the PLL kick it currently
  produces), but at large stage counts it is currently the only thing
  preventing a materially degraded first 10–100+ seconds. The evidence
  points at gating engagement on AGC convergence — which happens in ~20
  blocks regardless of stage count — rather than on a fixed block count that
  is too short for `-E100` and five times longer than necessary for `-E36`.
  See §11 — and then §18, which supersedes it.

---

# Part I — removing the wait entirely (0 versus 100 blocks)

## 1. Method

### 1.1 Builds

Two axes, four build variants, all from a single throwaway `git worktree`
checkout of `dev` at `2c792c7` (the tip at the start of this evaluation):

| variant | `MF_WAIT_BLOCKS` | extra flags | purpose |
| --- | --- | --- | --- |
| `w100` | 100 (default) | — | baseline, clean |
| `w0` | 0 | — | wait removed, clean |
| `w100-dbg` | 100 | `COEFF_MONITOR DEBUG_MULTIPATH_FILTER DEBUG_MF_RESET DEBUG_MF_ERR DEBUG_AGC_TRACE` | baseline, instrumented |
| `w0-dbg` | 0 | (same) | wait removed, instrumented |
| `w100-pll` / `w0-pll` | 100 / 0 | `DEBUG_PLL_FILTER` | pilot-PLL frequency trace (§10 only) |

Per `doc/MF_DEBUG_CODE_20260726.md`'s pattern, the wait is made
overridable at compile time in the worktree copy of `sfmbase/FmDecode.cpp`:

```cpp
#ifndef MF_WAIT_BLOCKS
#define MF_WAIT_BLOCKS 100
#endif
...
m_wait_multipath_blocks(MF_WAIT_BLOCKS), ...
```

`DEBUG_MF_RESET` (one line per divergence reset), `DEBUG_MF_ERR` (one line
per filtered block with the block's peak `|output|` and last `m_error`), and
`DEBUG_AGC_TRACE` (one line per block with the IF AGC's current gain) are new
instrumentation added only in the worktree, following the existing
`COEFF_MONITOR` / `DEBUG_MULTIPATH_FILTER` / `DEBUG_PLL_FILTER` convention —
stderr-only, `#ifdef`-guarded, no DSP arithmetic touched. All four flags
default off; a normal build is unaffected. `git diff --stat` in the worktree
for the two touched files: `FmDecode.cpp` +40/−3, `MultipathFilter.cpp`
+24/−1.

Environment: Apple M2 Pro, macOS 25.6.0 (Darwin), Homebrew clang 22.1.8,
`-O3 -ftree-vectorize -std=c++20`, VOLK 3.3.0. No `-ffast-math` anywhere, per
`CLAUDE.md`.

### 1.2 Recordings

All seven files in `test-files/`, 384 kHz float32 stereo IQ:

| file | tag | duration | character | freq used for decode |
| --- | --- | --- | --- | --- |
| `AirSpy_20260727_125800Z_89700kHz_IQ.wav` | airspy | 200 s | off-air, real multipath, documented PLL kick | 89700000 |
| `interfm-20260724102822z-iq.wav` | interfm | 100 s | off-air, real multipath | 82000000 |
| `joakfm-20260715045930z-iq.wav` | joakfm | 60 s | off-air, near-clean | 82500000 |
| `piano_iqtest.wav` | piano | 20 s | synthetic, clean reference | 82500000 |
| `piano_iqtest-a0p5-t5us.wav` | pa0p5 | 20 s | synthetic two-ray, a=0.5, τ=5 µs | 82500000 |
| `piano_iqtest-a0p9-t3us.wav` | pa0p9 | 20 s | synthetic two-ray, a=0.9, τ=3 µs | 82500000 |
| `piano_iqtest-a1p2-t8us.wav` | pa1p2 | 20 s | synthetic two-ray, a=1.2 (non-minimum-phase), τ=8 µs | 82500000 |

Total material per pass: 440 s. `filesource` paces to real time, so every
pass below took its wall-clock duration; decodes of independent
(file, stage-count, variant) combinations were run concurrently in the
background to keep total wall time down (26 decodes, all confirmed
`EXIT:0`, none exceeding 200 s wall time as a batch).

Decode command:

```sh
airspy-fmradion -m fm -t filesource -E <stages> \
  -c freq=<freq>,srate=384000,filename=test-files/<file>,wav,format=FLOAT \
  -q -G out.wav
```

`-q` is used throughout; `COEFF_MONITOR` and the new flags are unaffected by
quiet mode (`doc/MF_DEBUG_CODE_20260726.md` §7).

### 1.3 Coverage

| stage count | files |
| --- | --- |
| `-E36` (reference — old and new step-size rules coincide here) | all 7 |
| `-E18`, `-E100` | airspy, interfm |
| `-E100` (causal test) | pa0p9 |

26 decodes total (7×2 at `-E36`, 2×2×2 at `-E18`/`-E100`, 2 for the pa0p9
causal test, plus 4 `-pll`-instrumented decodes for §10).

---

## 2. What the wait means on each device

`m_wait_multipath_blocks` counts calls to `FmDecoder::process()`, one per
main-loop iteration, i.e. one per `if_blocksize`-sized chunk pulled from the
source at its **native** sample rate (`main.cpp:701-731`). Wait duration is
therefore `100 * if_blocksize / ifrate`, independent of any resampling to the
384 kHz decoder rate:

| device | `if_blocksize` | native rate (default) | wait duration |
| --- | --- | --- | --- |
| `FileSource` (this study) | 2048 | 384000 (fixed) | **0.5333 s** |
| Airspy HF+ | 2048 | 384000 (default; `srate=` overridable) | **0.5333 s** |
| Airspy R2 / Mini | 65536 | 10,000,000 (default; `srate=list` for others) | **0.6554 s** |
| RTL-SDR | 16384 | 1,152,000 (default; valid range 900,001–3,200,000) | **1.4222 s**, range 0.512–1.821 s |

The constant is a block count, not a time, and the actual wait varies by
2.7× across supported devices at their default configuration, and by more
if a non-default `srate=` is used on Airspy R2 or RTL-SDR. All measurements
below use the `FileSource`/`AirspyHF+` figure of 0.5333 s, since all seven
recordings are decoded through `filesource` at 384 kHz.

---

## 3. AGC settling versus the wait

The IF AGC (`IfSimpleAgc`, `initial_gain=1.0, max_gain=100000.0, rate=1e-4`)
runs immediately upstream of the wait/multipath-filter branch. Its gain
trajectory is identical between the `w100` and `w0` variants for a given
file — the AGC has no downstream feedback from the multipath filter — so one
trace per file suffices.

Checkpoint gain values (block period 5.333 ms), against `g_ref` = median
gain over t ∈ [2, 5] s and `K = g_ref / g_initial`:

| file | `g₀` | `g_ref` | K | g@53ms | g@107ms | g@213ms | g@533ms (old wait) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| airspy | 1.226 | 15.990 | 13.04 | 8.269 (51.7%) | 15.980 (99.9%) | 15.970 (99.9%) | 15.558 |
| interfm | 1.224 | 15.953 | 13.04 | 8.150 (51.1%) | 15.724 (98.6%) | 16.140 (101.2%) | 16.270 |
| joakfm | 1.226 | 14.212 | 11.60 | 7.942 (55.9%) | 14.024 (98.7%) | 14.198 (99.9%) | 14.096 |
| piano (clean) | 1.226 | 13.867 | 11.32 | 7.833 (56.5%) | 13.618 (98.2%) | 13.702 (98.8%) | 13.881 |
| pa0p5 | 0.988 | 0.990 | 1.00 | 0.973 | 0.978 | 0.983 | 0.978 |
| pa0p9 | 0.978 | 0.973 | 0.995 | 0.958 | 0.970 | 0.969 | 0.969 |
| pa1p2 | 0.975 | 0.968 | 0.993 | 0.945 | 0.952 | 0.969 | 0.944 |

Two distinct regimes:

- **The four off-air/near-clean recordings need a real gain ramp**, K ≈
  11.3–13.0 (raw IQ RMS 0.06–0.07, versus the AGC's unity target). Gain
  reaches ~99% of its quasi-steady value by **block 20 (t = 0.107 s)** —
  about a fifth of the 100-block wait. The remaining 80 blocks of the wait
  buy essentially nothing for AGC settling specifically (though the AGC
  gain continues to wander by a few percent for the whole file, tracking
  real signal-envelope and fading dynamics — see the non-monotonic values
  at t = 533 ms above — which is normal AGC behavior, not a settling
  artifact).
- **The three synthetic two-ray files are already at K ≈ 0.99–1.00.** They
  were generated at approximately unity RMS, so the AGC does effectively
  nothing beyond ordinary gain jitter. These files therefore isolate the
  multipath filter's own cold-start behavior from the AGC-mismatch effect,
  which is exploited directly in §8.

**The 100-block wait is about 5× longer than the AGC's own fast-convergence
time constant on every recording that needs one.** This is consistent with
the wait being a round, comfortable number rather than one derived from
`IfSimpleAgc`'s `rate = 1e-4` parameter.

---

## 4. Audio A/B at `-E36`, all seven recordings

Whole-file and post-1-second sample-by-sample difference between the `w100`
and `w0` clean builds, same methodology as `doc/MF_DIFFERENCE_20260727.md`
§1–2 (M = (L+R)/2, S = (L−R)/2, ratios relative to the `w100` signal):

| file | whole (dBFS) | after 1 s (dBFS) | M/M (dB) | S/S (dB) | correlation |
| --- | --- | --- | --- | --- | --- |
| airspy | −45.08 | **−60.26** | −28.26 | −26.55 | 0.99921 |
| interfm | −41.38 | **−63.26** | −25.86 | −25.57 | 0.99869 |
| joakfm | −49.67 | **−85.95** | −26.18 | −31.08 | 0.99892 |
| piano (clean) | −46.37 | **−79.16** | −27.07 | −24.79 | 0.99881 |
| pa0p5 | −46.88 | **−70.11** | −27.51 | −25.13 | 0.99892 |
| pa0p9 | −46.31 | **−63.90** | −27.48 | −23.67 | 0.99875 |
| pa1p2 | −46.66 | **−58.13** | −27.35 | −24.42 | 0.99884 |

All seven recordings converge to a difference floor 38–86 dB below signal
level after the first second, and correlation is ≥ 0.9987 whole-file in
every case. This floor is **higher** than the −128 dBFS float-noise floor
seen when comparing two bit-compatible builds in
`doc/MF_DIFFERENCE_20260727.md` — see §5 for why: the two variants settle
onto slightly different (but both stable) points in coefficient space, which
produces a small, persistent, non-transient audio difference on top of the
startup transient.

### Where the difference energy lives

Windowed (10 ms) difference-to-signal energy share, first 2 s, `-E36`:

| file | 0–50 ms | 50–100 ms | 100–200 ms | 200–500 ms | 500 ms–1 s | 1–2 s |
| --- | --- | --- | --- | --- | --- | --- |
| airspy | 12.4% | 4.1% | 4.8% | **66.5%** | 12.1% | 0.11% |
| interfm | 10.3% | 8.4% | 10.6% | **62.3%** | 8.4% | 0.01% |
| joakfm | **36.4%** | 11.3% | 5.9% | 28.2% | 18.2% | 0.01% |
| piano (clean) | 5.8% | 2.2% | 9.4% | **75.3%** | 7.4% | 0.00% |

**> 99.9 % of the difference energy in the first two seconds lands before t
= 1 s** in every case, confirming the difference is a startup phenomenon,
not a lasting one. The plurality of it (28–75%) sits in the 200–500 ms
window — which is not a coincidence: through that whole span `w100` is
still bypassing the filter entirely (raw AGC output) while `w0` has already
been actively filtering (and adapting) since t = 0, so the two audio streams
differ continuously across the *whole* pre-engagement span, not only at a
single instant. `w100` then adds its own sharp engagement transient right at
t = 533 ms (the already-documented step discontinuity), which folds into the
same 200–500 ms bucket at 10 ms resolution.

---

## 5. Coefficient trajectory and convergence

`COEFF_MONITOR` dumps (one per ~1.067 s, all taps) compared tap-by-tap
between `w100` and `w0` at matching block numbers: `‖w0 − w100‖ / ‖w100‖` in
dB, and each build's own off-reference tap energy `Σ|w|² − 1`.

**At `-E18` and `-E36`, the discrepancy plateaus quickly and stays flat**
for the whole run (both the 200 s airspy and the 100 s interfm recording):

| file | `-E` | reldb @ t=1s | reldb @ t=5s | reldb @ t=50s | reldb @ end | trend |
| --- | --- | --- | --- | --- | --- | --- |
| airspy | 18 | −9.04 | −10.12 | −10.33 | −10.10 (t=199s) | flat |
| airspy | 36 | −7.85 | −10.06 | −10.36 | −10.28 (t=199s) | flat |
| interfm | 18 | −13.06 | −13.36 | −13.13 | −12.86 (t=99s) | flat |
| interfm | 36 | −14.93 | −15.41 | −15.19 | −15.01 (t=99s) | flat |

A stable −10 to −15 dB offset that neither grows nor shrinks over 100–200 s
of running time is consistent with the two builds converging to two nearby
points on the CMA cost function's flat directions (global phase / small
delay, per `doc/MULTIPATH_FILTER_DESIGN_20260724.md` §8.5) rather than
instability — benign.

**At `-E100`, it does not plateau.** Both real recordings show the
discrepancy climbing for the whole run and the `w0` off-reference tap energy
growing without settling back to the baseline:

| file | reldb @ t=1s | reldb @ t=5s | reldb @ t=50s | reldb @ end | off-ref energy `w0` @ t=1s → end | off-ref energy `w100` @ t=1s → end |
| --- | --- | --- | --- | --- | --- | --- |
| airspy (200 s) | +0.34 | +1.46 | +4.67 | **+4.47** (t=199s) | 0.86 → 3.75 | 0.22 → 0.68 |
| interfm (100 s) | −0.03 | +1.08 | +4.60 | **+4.91** (t=99s) | 0.76 → 3.18 | 0.35 → 0.50 |

A positive `reldb` means the two coefficient vectors differ by *more* than
either one's own norm — not a small offset, a genuinely different solution.
`w0`'s off-reference energy is 5–6× the `w100` baseline by the end of both
recordings, and it is still trending upward at the last dump of the longer
(200 s) recording, not clearly plateaued. **This does not trip the
divergence guard** (§6), but it is not the same converged filter the wait
produces, for as long as either recording runs.

The synthetic two-ray files at `-E36` (ground truth: the correct converged
solution is known) all converge cleanly regardless of the wait, and the
discrepancy *shrinks* as the run proceeds rather than growing or plateauing
at an elevated level:

| file | reldb @ t=1.07s | reldb @ end (t=19.2s) | off-ref `w100`/`w0` @ end |
| --- | --- | --- | --- |
| pa0p5 | −20.68 | **−44.56** | 0.4106 / 0.4159 |
| pa0p9 | −15.65 | **−30.19** | 3.673 / 3.657 |
| pa1p2 | −11.02 | **−32.20** | 2.324 / 2.317 |

(The block-0 dumps, at −36.32/−24.37/−27.47 dB, are not shown here — both
builds start from the identical hard-coded identity coefficients, so the
apparent close agreement at t=0 is by construction, not evidence of
anything.) `off-ref` energy for `w100` and `w0` track each other closely and
grow together throughout the run in every file — that growth is this
channel's own ~15–20 s convergence time at `alpha=0.1`, present in both
builds equally, not a wait-related effect. Excellent agreement in every
case — at `-E36` the wait makes no measurable difference to what the filter
converges to on a channel with a known answer, and reconvergence *improves*
with time rather than stalling.

---

## 6. CMA error trajectory and the divergence guard

**Zero divergence resets** (`MFRESET` / `DEBUG_MF_RESET`) in all 26 decodes,
across every file, every stage count tested (18, 36, 100), and both wait
settings.

Peak filter output magnitude, worst block anywhere in each run, against
`divergence_limit = 10.0`:

| file | `-E` | peak, `w0` | headroom, `w0` | peak, `w100` | headroom, `w100` |
| --- | --- | --- | --- | --- | --- |
| airspy | 18 | 1.634 | 15.7 dB | 1.384 | 17.2 dB |
| airspy | 36 | 1.824 | 14.8 dB | 1.406 | 17.0 dB |
| airspy | 100 | **2.800** | **11.1 dB** | 1.431 | 16.9 dB |
| interfm | 18 | 1.643 | 15.7 dB | 1.345 | 17.4 dB |
| interfm | 36 | 1.835 | 14.7 dB | 1.387 | 17.2 dB |
| interfm | 100 | **2.738** | **11.3 dB** | 1.463 | 16.7 dB |

Removing the wait costs 2.2–3.7 dB of divergence headroom at `-E18`/`-E36`
and about 5.6–5.8 dB at `-E100` — real, but the worst case measured
(11.1 dB) is still a factor of 3.6 below the trip point, not close to it.

### First-second and beyond: mean |CMA error|, ratio `w0`/`w100`

| file | `-E` | 0–1s | 1–2s | 2–5s | 5–10s | 10–20s | 20–50s | 50–100s | 100–200s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| airspy | 36 | 1.84× | 1.28× | 1.11× | 1.01× | 1.01× | 1.00× | 1.01× | 1.00× |
| interfm | 36 | 1.29× | 1.23× | 1.04× | 0.99× | 1.00× | 1.00× | 1.00× | — |
| airspy | 100 | 7.43× | 29.1× | 26.7× | 8.63× | 3.15× | 2.72× | 1.98× | 1.77× |
| interfm | 100 | 11.1× | 27.3× | 29.7× | 27.6× | 30.0× | 10.6× | 1.07× | — |

At `-E36` the error ratio is back to 1.00–1.01 (i.e., no measurable
difference) by 5–10 s. At `-E100` it is still 1.8–2× elevated at the *end*
of the 200 s airspy recording, and stays 10–30× elevated through 5–20 s on
the 100 s interfm recording before dropping close to 1.0× only in the last
50 s of the file. This matches §5's coefficient-trajectory finding: `-E100`
with the wait removed is degraded for a long time, not just at the start.

---

## 7. Stage-count sensitivity — whole-file audio impact

Same A/B methodology as §4, now across `-E18`/`-E36`/`-E100` on the two real
recordings:

| file | `-E` | whole (dBFS) | after 1s (dBFS) | correlation |
| --- | --- | --- | --- | --- |
| airspy | 18 | −47.23 | −59.97 | 0.99952 |
| airspy | 36 | −45.08 | −60.26 | 0.99921 |
| airspy | 100 | **−15.48** | **−15.48** | **0.27590** |
| interfm | 18 | −45.57 | −61.26 | 0.99950 |
| interfm | 36 | −41.38 | −63.26 | 0.99869 |
| interfm | 100 | **−22.28** | **−22.39** | **0.88817** |

At `-E18` and `-E36` the "after 1 s" figure is 20–48 dB *lower* than the
whole-file figure — most of the whole-file difference is the first second,
exactly as in §4. **At `-E100` the two figures are identical**, because the
difference never recovers: on airspy the difference-to-signal ratio is
+1.56 dB — the difference between the two decodes is *louder* than the
signal itself — for the entire 200 s file, and correlation between the two
audio streams falls to 0.276, barely more related than noise. interfm is
somewhat less severe (correlation 0.888) but still nowhere near the
0.9987–0.9995 seen at `-E18`/`-E36`. This is the audio-domain expression of
the persistent coefficient divergence in §5.

---

## 8. Isolating the mechanism: AGC mismatch versus tap count

§3 established that the four off-air/near-clean recordings need a large AGC
gain ramp (K ≈ 11–13) while the three synthetic two-ray files need almost
none (K ≈ 0.99–1.00). If the `-E100` pathology in §5–§7 is driven by the
interaction between a large, AGC-mismatched cold start and a filter with
many taps, then running `-E100` with the wait removed on a K ≈ 1 file should
converge cleanly. It does:

`piano_iqtest-a0p9-t3us.wav` (a = 0.9, the deepest of the three synthetic
echoes), `-E100`, `w100` vs `w0`:

| quantity | value |
| --- | --- |
| coefficient reldb, t=1.07s → t=19.2s | −15.49 dB → **−28.97 dB** (improving, not diverging) |
| off-reference energy, `w0` vs `w100` @ t=19.2s | 3.079 vs 3.073 (agree to 0.2%) |
| divergence resets | 0 (either build) |
| peak filter output, `w0` / `w100` | 1.489 / 1.545 (`w0` is *not* elevated) |
| whole-file audio diff-to-signal | −18.83 dB |
| after-1s audio diff-to-signal | **−42.03 dB** — the same kind of floor as `-E18`/`-E36` |
| correlation | **0.9935** (vs. 0.276–0.888 on the real recordings at `-E100`) |

Every symptom that appears on the real recordings at `-E100` — growing
coefficient divergence, elevated peak output, a post-1-second audio floor
that never recovers — is **absent** here. The only variable changed is the
input's initial amplitude relative to the AGC's unity target.

**Conclusion: the `-E100` + wait-removed pathology is not a property of a
large tap count or a cold delay line by themselves.** `-E100` with an empty
delay line converges fine, on this evidence, when the AGC has nothing to do.
It is specifically the combination of (a) a filter order large enough that
most taps carry no real echo energy and are therefore free to be driven by
gradient noise (`doc/MULTIPATH_FILTER_DESIGN_20260724.md` §9, §15) with (b)
an AGC that is still several factors away from its target amplitude during
the filter's most sensitive early updates, that produces the persistent
misadjustment. Only one synthetic file was tested at `-E100`; this is a
mechanism finding from a single confirming case, not an exhaustive sweep.

---

## 9. Pilot-PLL interaction

`DEBUG_PLL_FILTER` (already present in `sfmbase/PilotPhaseLock.cpp`, gated
behind a commented-out `#define`) was enabled to trace `m_freq` once per
block on the airspy recording, `-E36` and `-E100`, both wait settings.

### `-E36`: the known kick is replaced by ordinary acquisition jitter

| build | freq @ t=528ms | freq @ t=539ms | Δ | freq std, t∈[0,5s] |
| --- | --- | --- | --- | --- |
| `w100` | 18999.13 Hz | 19019.00 Hz | **+19.9 Hz** kick | 1.483 Hz |
| `w0` | 18999.56 Hz | 18999.55 Hz | none | 1.799 Hz |

(The measured kick here is a block-level, 5.33 ms-resolution sample of the
event that `doc/MF_PLL_DIFFERENCE_20260728.md` §6 measured at +22.36 Hz with
a finer 6 kHz trace — same event, same order of magnitude, coarser
sampling.) With the wait removed, the loop's own acquisition (which happens
regardless, since it starts from `m_freq = pilot_freq` at t = 0) absorbs the
filter's cold start instead of being hit by a separate step after settling.
The two builds' frequency-jitter standard deviation over the first 5 s
(1.48 Hz vs 1.80 Hz) is close enough that this is not a meaningfully worse
acquisition — **the kick is removed at essentially no cost, at this stage
count.**

### `-E100`: the known kick is replaced by ~10 seconds of limiter railing

| build | freq std, 0–1s | 1–2s | 2–5s | 5–10s | 10–20s | railed blocks, 0–5s |
| --- | --- | --- | --- | --- | --- | --- |
| `w100` | 2.415 Hz | 0.092 Hz | 0.044 Hz | 0.034 Hz | 0.037 Hz | 0/938 (0.0%) |
| `w0` | **20.80 Hz** | **19.12 Hz** | **19.56 Hz** | 8.33 Hz | 0.115 Hz | **123/938 (13.1%)** |

With the wait in place, `w100`'s only disturbance at `-E100` is the single,
bounded +7.2 Hz kick at t = 0.539 s (block-level sample of the +8.36 Hz
event in `MF_PLL_DIFFERENCE_20260728.md` §6), after which jitter is at its
steady-state floor (~0.03–0.04 Hz) by t = 2 s. With the wait removed, the
loop's frequency std is 20–80× higher than baseline for the first 5 seconds,
and it hits the ±30 Hz frequency limiter on 12–16% of blocks in that window
— compared to never, for `w100`, at any stage count tested. **The PLL's own
jitter recovers to the baseline order of magnitude by about t = 10 s** (std
0.115 Hz at 10–20s, versus the filter's coefficient state, which per §5 has
not recovered even by t = 199 s) — so the PLL disturbance is shorter-lived
than the underlying coefficient misadjustment, but it is severe while it
lasts: 13% of blocks briefly open-loop in the first five seconds is a
qualitatively different, and worse, event than the single bounded kick the
wait currently produces at this setting.

**Net effect on the PLL, by stage count:** the wait removes a
well-characterized, single, bounded transient and — at large stage counts —
replaces it with a materially worse (though shorter-lived than the filter's
own recovery) multi-second disturbance. At the common stage counts it is a
clear improvement; at large stage counts it is a clear regression.

---

## 10. What this does and does not establish

- **It establishes, with direct measurement, that the wait is not needed
  for correctness at `-E18`–`-E36`** on 7 recordings including two real
  off-air multipath channels and three synthetic two-ray channels with a
  known correct answer, and that removing it there is a modest net
  *improvement* (no lasting audio difference, and the documented PLL kick
  disappears).
- **It establishes, with direct measurement on two independent real
  recordings, that removing the wait at `-E100` produces a persistent
  degradation** — not literal instability by the divergence guard's
  criterion, but a materially different, worse-converged filter and a
  correspondingly damaged pilot signal for a long time (tens of seconds to,
  on the 200 s airspy file, the entire remainder of the recording).
- **It identifies the mechanism** (AGC-mismatch cold start interacting with
  a large tap count) with one confirming causal test, not an exhaustive
  sweep. The threshold stage count at which the pathology begins to appear
  was not bracketed — only `-E18`, `-E36`, and `-E100` were tested, and the
  jump in severity between `-E36` (clean) and `-E100` (severely degraded)
  is large enough that the actual onset could be anywhere in between. A
  finer sweep (e.g. `-E50`, `-E70`, matching the stage counts already used
  in `doc/MF_PLL_DIFFERENCE_20260728.md`) was not run here.
- **It does not test hardware sources.** All measurements are through
  `filesource`; §2's device-dependent wait durations are computed from the
  block-size/sample-rate arithmetic in `main.cpp`, not measured on live
  Airspy/RTL-SDR hardware. An SDR source's IQ level at power-on could differ
  from a pre-recorded file's, which would change the AGC's initial mismatch
  factor K and could shift where the `-E`-dependent threshold in the
  paragraph above sits.
- **It does not evaluate audio quality against ground truth at `-E100`.**
  §5's ground-truth two-ray test was run at `-E36`, not `-E100`, for the
  three files with a known correct answer; the `-E100` causal test in §8
  used only the deepest-echo file (`pa0p9`) and confirmed clean convergence,
  but did not separately score against the two-ray SNR harness of
  `doc/MULTIPATH_FILTER_DESIGN_20260724.md` §6/§18.
- **It does not evaluate fading channels specifically.** interfm and airspy
  both contain real, if modest, fading; neither was chosen to contain a deep
  fade coincident with startup. `doc/MULTIPATH_FILTER_DESIGN_20260724.md`
  already establishes that the filter is a net loss on fading channels
  regardless of this wait; nothing here changes that conclusion or extends
  it to the wait-removed case specifically.

---

## 11. Recommendation (Part I — superseded by §18)

> **Superseded.** This section was written when only the endpoints 0 and 100
> had been measured, and its closing paragraph declines to recommend any
> intermediate value on the grounds that none had been tested. Part II tests
> 20 and 50 across the full grid and brackets the onset at 5–10 blocks, which
> makes the middle ground a validated option. **See §18.** The reasoning below
> is retained because its first two paragraphs — why blanket removal is unsafe,
> and why the AGC is the relevant axis — still hold; only the final paragraph's
> conclusion is overturned.

**Do not remove the wait unconditionally.** At the stage counts most
commonly documented and used (`-E18`–`-E36`), doing so is safe and a small
net improvement — it eliminates the wait's own self-inflicted PLL kick with
no measurable lasting cost. But at `-E100` it produces a real, measured
degradation that persists far longer than any reasonable definition of a
"startup transient," confirmed on two independent real off-air recordings
and explained by a targeted causal test. A change that is good at one end of
the supported `-E` range and actively harmful at the other is not safe to
ship as a blanket removal.

**Better direction, supported by §3's measurement: gate engagement on AGC
convergence rather than on a fixed block count.** The AGC reaches ~99% of
its quasi-steady gain in about 20 blocks regardless of the eventual stage
count — the wait's problem is not that it exists, but that it is a constant
sized for one axis (device block rate) while the actual failure mode in §5–§9
is driven by a different, cheaply observable quantity: how far the AGC's
current gain is from settled. A concrete version: track the AGC gain's
fractional change over a short recent window (already computable from
`IfSimpleAgc::get_current_gain()`, no new state needed in the AGC itself)
and hold the multipath filter's bypass until that change falls under a
threshold for some minimum number of consecutive blocks, instead of a fixed
100. This would plausibly:

- shorten the wait to roughly the ~20 blocks §3 measures on real recordings
  (removing most of the currently-unnecessary 0.4 s and thus most of the
  wait's self-inflicted PLL kick even when the wait is kept), and
- lengthen it automatically on any source or setting where the AGC takes
  longer than usual to settle, which — per §8's mechanism finding — is
  exactly the condition under which the `-E100` pathology appears.

This was not implemented or measured here; it is a design direction the
evidence in §3–§9 points at, offered per the brief. Any such change should
be validated the same way this evaluation was: real off-air recordings at
both common and large `-E`, the divergence-reset count, and the coefficient
convergence trajectory beyond the first few seconds, not just the first-second
audio floor that dominates the `-E18`/`-E36` case here.

**If a smaller, purely conservative change is wanted instead:** leave the
wait exactly as it is. It is currently the only thing standing between
`-E100` and the persistent misadjustment in §5–§9, and this evaluation did
not bracket the stage count at which that risk begins, so "reduce the wait's
block count" is not a validated middle ground on the evidence gathered here
— only "0" and "100" were tested, and their outcomes at `-E100` are
opposite.

> **This last paragraph is what §18 overturns.** Part II measured the middle
> ground directly: 20 blocks is at the 100-block baseline on every metric and
> on every recording, and the onset of the `-E100` failure sits between 5 and
> 10 blocks. "Reduce the wait's block count" *is* now a validated option.

---

## 12. Reproduction

```sh
git worktree add --detach /tmp/mfwait dev
# copy r8brain-free-src into the worktree and remove its .git gitlink,
# since it is a submodule of the main tree:
cp -R r8brain-free-src /tmp/mfwait/ && rm -f /tmp/mfwait/r8brain-free-src/.git

# In /tmp/mfwait/sfmbase/FmDecode.cpp, wrap the wait constant:
#   #ifndef MF_WAIT_BLOCKS
#   #define MF_WAIT_BLOCKS 100
#   #endif
#   ...
#   m_wait_multipath_blocks(MF_WAIT_BLOCKS), ...
# and add DEBUG_MF_RESET / DEBUG_MF_ERR / DEBUG_AGC_TRACE prints as described
# in §1.1 (stderr-only, #ifdef-guarded).
#
# This compile-time variant was not kept. To reproduce Part I, use the
# round-2 harness of §19 (doc/MULTIPATH_QMM_20260813_harness.diff) and set
# MF_WAIT_BLOCKS=0 or 100 instead: §13.1 verified that it reproduces this
# part's outputs bit-exactly.

cmake -S /tmp/mfwait -B /tmp/mfwait/build-w100
cmake -S /tmp/mfwait -B /tmp/mfwait/build-w0 -DEXTRA_FLAGS="-DMF_WAIT_BLOCKS=0"
cmake -S /tmp/mfwait -B /tmp/mfwait/build-w100-dbg \
  -DEXTRA_FLAGS="-DCOEFF_MONITOR=1 -DDEBUG_MULTIPATH_FILTER=1 -DDEBUG_MF_RESET=1 -DDEBUG_MF_ERR=1 -DDEBUG_AGC_TRACE=1"
cmake -S /tmp/mfwait -B /tmp/mfwait/build-w0-dbg \
  -DEXTRA_FLAGS="-DCOEFF_MONITOR=1 -DDEBUG_MULTIPATH_FILTER=1 -DDEBUG_MF_RESET=1 -DDEBUG_MF_ERR=1 -DDEBUG_AGC_TRACE=1 -DMF_WAIT_BLOCKS=0"
cmake -S /tmp/mfwait -B /tmp/mfwait/build-w100-pll -DEXTRA_FLAGS="-DDEBUG_PLL_FILTER=1"
cmake -S /tmp/mfwait -B /tmp/mfwait/build-w0-pll -DEXTRA_FLAGS="-DDEBUG_PLL_FILTER=1 -DMF_WAIT_BLOCKS=0"
for d in build-w100 build-w0 build-w100-dbg build-w0-dbg build-w100-pll build-w0-pll; do
  cmake --build /tmp/mfwait/$d --target airspy-fmradion -j
done
```

Decode, per §1.2's file/frequency table, `-q -G out.wav`, then for the
audio comparison in §4/§7/§8:

```python
import numpy as np, soundfile as sf
a, sr = sf.read("w100.wav", dtype="float64", always_2d=True)
b, _  = sf.read("w0.wav",   dtype="float64", always_2d=True)
n = min(len(a), len(b)); a, b = a[:n], b[:n]
db = lambda v: 20 * np.log10(np.sqrt(np.mean(v ** 2)) + 1e-300)
d = a - b
print("whole:", db(d), "after1s:", db(a[int(sr):] - b[int(sr):]))
print("corr:", np.corrcoef(a.flatten(), b.flatten())[0, 1])
```

`COEFF_MONITOR` lines (`block,<n>,mf_error,<e>,mf_coeff,<i,re,im,...>`) parse
as in `doc/MF_ENERGY_20260726.md` §7; `MFERR,<peak>,<error>,<reset_flag>`
lines from the new `DEBUG_MF_ERR` flag are emitted once per filtered block,
in order, so the block's true index is `wait_blocks + line_index` (0-based)
when the wait is nonzero. `AGCTRACE,<block>,<gain>,<still_waiting>` lines
from `DEBUG_AGC_TRACE` are one per `FmDecoder::process()` call regardless of
wait state. `DEBUG_PLL_FILTER`'s existing
`m_freq = ..., m_freq_err = ..., m_pilot_level = ...` line is unchanged from
the source tree and fires once per block (`i == 0`).

---

# Part II — the 20-block and 50-block wait

Part I tested only the two endpoints, 0 and 100, and §11 explicitly declined to
recommend anything in between: *"'reduce the wait's block count' is not a
validated middle ground on the evidence gathered here — only '0' and '100' were
tested, and their outcomes at `-E100` are opposite."* This part fills that gap
by measuring waits of **20** and **50** blocks across every recording and every
stage count, and then brackets the failure onset below 20. It supersedes §11;
see §18.

## 13. Round-2 method

### 13.1 One binary, four waits

The wait is now settable at **run time** from the `MF_WAIT_BLOCKS` environment
variable instead of the compile-time macro Part I used, so all four wait values
come from a single build rather than four. The helper parses the variable once
into a function-local static, rejects negative/non-numeric/out-of-range input
with a fallback to 100, and echoes the value actually used as `MFWAIT,<n>` on
stderr so every log records its own setting unambiguously. Only
`sfmbase/FmDecode.cpp` is touched (+103/−8) in the throwaway worktree
`/tmp/mfwait2`; the main tree is untouched.

**The macro → env-var switch was verified inert before any of it was
believed.** Decoding `piano_iqtest-a0p9-t3us.wav` at `-E36` through the new
build at `MF_WAIT_BLOCKS=0` and `=100` reproduces Part I's corresponding
outputs **bit-exactly** (`cmp`, not a tolerance). Every w0/w100 number in the
tables below independently reproduces Part I's to all printed digits — e.g.
airspy `-E100` w0 correlation 0.275904 here versus 0.27590 in §7, interfm
0.888168 versus 0.88817.

A second confound in Part I is also removed: there, the audio comparison and
the instrumented coefficient dumps came from *different* builds (clean vs
`-dbg`). Here every cell in the grid is produced by the single `-dbg` binary,
so audio, coefficients, AGC trace, and reset counts for a given cell all come
from the same process.

### 13.2 Determinism, and why the decodes could be run concurrently

The grid is 105 decodes totaling 8,240 s of real-time-paced material (5,280 s
for the main grid, 2,000 s for the PLL traces, 960 s for the onset bracket), so
it was run 12-way concurrent. That is only legitimate if the output does not
depend on wall-clock timing. Tracing the pipeline:

- Nothing in the DSP path is wall-clock-derived. `Utility::get_time()` in the
  main loop (`main.cpp:916,951-952`) feeds only the optional PPS file and the
  status display (`main.cpp:1141-1142,1159`); it never reaches `fm.process()`,
  the resamplers, the AGC, or the multipath filter. The chain is driven purely
  by sample count.
- The `-G` sink is a synchronous `sf_write_float()` called from the main loop
  (`sfmbase/AudioOutput.cpp:154-166`), with none of `PortAudioOutput`'s pacing
  or underrun logic. It cannot skip or duplicate samples.
- Only two threads exist per process (the `Source` thread and the main thread);
  block processing is strictly sequential.
- VOLK kernel dispatch resolves once from CPU feature detection — a fixed
  property of the machine, not of load.

There is exactly **one** timing hazard: `DataBuffer` is a bounded queue with a
**drop-oldest** policy — `max_queue_blocks = 1024` (`include/DataBuffer.h:40`),
and `push()` discards the oldest block once the queue exceeds it
(`DataBuffer.h:55-58`), printing `DataBuffer: queue overflow, dropped blocks`.
`FileSource::run()` paces against `steady_clock`, not against the consumer's
drain rate, and never inspects queue depth (`sfmbase/FileSource.cpp:403-467`),
so a consumer that falls far enough behind loses samples irrecoverably. At
2048-sample blocks and 384 kHz that budget is 1024 blocks ≈ **5.46 s** of lag.
Below it, lag costs only wall-clock time, not correctness.

That makes the overflow message a necessary and sufficient run-level check.
**It appeared in none of the 105 decodes**, so every result below is bit-exact.
Empirically there was ample headroom: a single `-E100` instrumented decode uses
3.44 s of CPU over 20.0 s of wall clock (**19.3%** of one core), so 12 concurrent
decodes draw ~2.3 of 10 cores. The 84-decode main grid completed in 7 min 21 s.

### 13.3 Coverage

| axis | values |
| --- | --- |
| recordings | all 7 of §1.2 |
| stage counts | `-E18`, `-E36`, `-E100` |
| wait values | 0, 20, 50, 100 |
| main grid | 7 × 3 × 4 = **84 decodes**, all `EXIT:0` |
| pilot-PLL trace (`DEBUG_PLL_FILTER`) | airspy `-E36`/`-E100`, interfm `-E100`, × 4 waits = **12 decodes** |
| onset bracket (§17) | airspy/interfm/piano `-E100` at waits 5, 10, 15 = **9 decodes** |

Every cell is compared against the **w100 decode of the same recording and
stage count**, so "0.00 dB" / `-6000` entries on the w100 column are the
self-comparison and simply confirm the pairing.

---

## 14. Audio: the wait of 20 blocks already fixes `-E100`

Difference from the w100 baseline, measured after the first second (dBFS),
which §4 established is the figure that separates a startup transient from a
lasting change:

### `-E18`

| file | w0 | w20 | w50 |
| --- | --- | --- | --- |
| airspy | −59.97 | −75.60 | −77.52 |
| interfm | −61.26 | −81.64 | −91.03 |
| joakfm | −90.61 | −107.45 | −111.41 |
| piano | −78.51 | −92.60 | −94.65 |
| pa0p5 | −69.87 | −70.22 | −72.81 |
| pa0p9 | −63.84 | −64.81 | −68.78 |
| pa1p2 | −58.53 | −58.89 | −62.42 |

### `-E36`

| file | w0 | w20 | w50 |
| --- | --- | --- | --- |
| airspy | −60.26 | −73.64 | −76.88 |
| interfm | −63.26 | −81.87 | −87.83 |
| joakfm | −85.95 | −107.21 | −111.01 |
| piano | −79.16 | −93.41 | −95.94 |
| pa0p5 | −70.11 | −70.50 | −73.02 |
| pa0p9 | −63.90 | −64.71 | −68.54 |
| pa1p2 | −58.13 | −58.66 | −61.85 |

### `-E100`

| file | w0 | w20 | w50 |
| --- | --- | --- | --- |
| airspy | **−15.48** | −73.21 | −75.94 |
| interfm | **−22.39** | −79.32 | −82.31 |
| joakfm | −83.91 | −105.62 | −109.47 |
| piano | **−16.19** | −93.84 | −96.49 |
| pa0p5 | −69.95 | −70.35 | −72.74 |
| pa0p9 | −62.31 | −63.09 | −67.12 |
| pa1p2 | −58.28 | −59.08 | −62.15 |

Correlation with the w100 decode over the whole file, `-E100`:

| file | w0 | w20 | w50 |
| --- | --- | --- | --- |
| airspy | **0.275904** | 0.998839 | 0.999111 |
| interfm | **0.888168** | 0.998179 | 0.998684 |
| joakfm | 0.996200 | 0.997889 | 0.998601 |
| piano | **−0.235228** | 0.993460 | 0.995704 |
| pa0p5 | 0.993591 | 0.993996 | 0.996137 |
| pa0p9 | 0.993455 | 0.993843 | 0.995978 |
| pa1p2 | 0.994317 | 0.994780 | 0.996792 |

**A wait of 20 blocks recovers the entire `-E100` deficit.** On airspy the
after-1s difference improves by **57.7 dB** (−15.48 → −73.21) and correlation
by 0.72; on piano by **77.7 dB**. Every `-E100` cell at w20 and w50 lands in
the same −59 to −110 dBFS band as the corresponding `-E18`/`-E36` cells, i.e.
the stage-count dependence that dominates Part I's conclusion **disappears
entirely once the wait is 20 blocks**. Going from 20 to 50 buys a further
2–6 dB — real but small, and a fifth the size of the w0 → w20 step.

At `-E18` and `-E36`, where Part I found w0 already acceptable, w20 is still a
consistent 13–21 dB improvement over w0 on the four AGC-ramping recordings and
a wash (0.3–1.0 dB) on the three synthetic two-ray files, which need no AGC
ramp at all.

### 14.1 Where the difference energy sits in time (`-E100`)

Difference from w100 by window, dBFS:

| file | w | 0–0.5 s | 0.5–1 s | 1–2 s | 2–5 s | 5–10 s | 10–30 s | 30 s–end |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| airspy | 0 | −17.0 | −18.3 | −16.9 | −17.1 | −14.5 | −14.3 | −15.6 |
| airspy | 20 | −17.6 | −31.0 | −51.8 | −72.0 | −75.3 | −75.3 | −79.2 |
| airspy | 50 | −18.8 | −31.1 | −54.8 | −74.8 | −77.7 | −77.6 | −81.5 |
| interfm | 0 | −16.8 | −17.3 | −19.4 | −17.6 | −17.6 | −17.5 | −30.4 |
| interfm | 20 | −17.3 | −27.9 | −62.4 | −78.3 | −81.3 | −82.3 | −82.6 |
| piano | 0 | −17.5 | −18.2 | −24.2 | −16.6 | −19.4 | −14.8 | — |
| piano | 20 | −23.1 | −38.9 | −86.7 | −87.7 | −101.4 | −105.3 | — |

w0 is flat across the whole file — it never recovers. w20 decays monotonically
and is already 50–70 dB down by 2 s. This is the clearest single expression of
the difference between the two: **w0's `-E100` error is a permanent state, w20's
is a startup transient.**

### 14.2 The damage is upstream of the stereo decoder

Splitting the `-E100` w0 difference into mid and side, relative to the
baseline's own M and S levels:

| file | w | M diff rel | S diff rel |
| --- | --- | --- | --- |
| airspy | 0 | **+1.47 dB** | **+2.21 dB** |
| piano | 0 | **+3.59 dB** | **+4.50 dB** |
| airspy | 20 | −26.59 | −24.74 |
| piano | 20 | −19.86 | −17.17 |

Both M and S are damaged roughly equally, and at w0 the difference exceeds the
signal in *both*. This is the diagnostic signature of a fault **upstream of the
FM discriminator** — exactly where the multipath filter sits — and is the
mirror image of the S-only signature that `doc/MF_DIFFERENCE_20260727.md` §2
used to *rule out* the multipath filter for a different discrepancy. It
independently confirms that what w0 breaks at `-E100` is the filter itself, not
stereo recovery downstream of it.

---

## 15. Coefficients

Off-reference tap energy at end of run (the w100 baseline in the last column),
and `reldb` — the coefficient-vector distance to the w100 decode at matched
blocks, where 0 dB means the two solutions differ by as much as their own norm:

### `-E100`

| file | w | reldb @1 s | @5 s | @50 s | @end | off-ref @end | w100 off-ref |
| --- | --- | --- | --- | --- | --- | --- | --- |
| airspy | 0 | −1.51 | +0.27 | +0.27 | **−0.04** | **3.7529** | 0.6826 |
| airspy | 20 | −12.06 | −28.93 | −29.06 | −29.06 | 0.6628 | 0.6826 |
| airspy | 50 | −14.91 | −31.19 | −31.27 | −31.29 | 0.6671 | 0.6826 |
| interfm | 0 | −1.20 | −0.30 | +0.45 | **+0.45** | **3.1812** | 0.4978 |
| interfm | 20 | −23.23 | −34.42 | −34.40 | −34.47 | 0.5046 | 0.4978 |
| interfm | 50 | −24.79 | −39.54 | −40.06 | −39.97 | 0.4947 | 0.4978 |
| piano | 0 | −3.13 | −2.20 | — | **−1.51** | **2.0170** | 0.0104 |
| piano | 20 | −38.57 | −48.70 | — | −62.88 | 0.0104 | 0.0104 |
| joakfm | 0 | −17.37 | −19.34 | −22.28 | −22.38 | 0.0173 | 0.0107 |
| joakfm | 20 | −40.89 | −47.27 | −67.75 | −68.40 | 0.0107 | 0.0107 |

The coefficient domain tells the same story more sharply than the audio.
At w0 the `-E100` filter sits at `reldb ≈ 0` — a completely different solution
— and carries 5.5× (airspy), 6.4× (interfm) or **194×** (piano) the baseline's
off-reference tap energy, for the whole run. At w20 `reldb` falls to −29 to
−63 dB and **stops falling by about 5 s**, i.e. it converges and then stays
converged rather than slowly drifting; off-reference energy matches the
baseline to within 3%.

Peak filter output over the run, `-E100`: airspy 2.800 (w0) → 1.440 (w20),
against the baseline's 1.431 and a `divergence_limit` of 10. **The divergence
guard fired zero times in all 105 decodes**, at every wait and stage count —
consistent with §6.

### 15.1 A refinement to Part I's `-E18`/`-E36` conclusion

Part I concluded that removing the wait is harmless at `-E18`/`-E36`. The
coefficient data, which Part I only examined at `-E36` on the ground-truth
files, shows that is true *audibly* but not *exactly*:

| file | `-E` | w | reldb @end | off-ref @end | w100 off-ref |
| --- | --- | --- | --- | --- | --- |
| airspy | 18 | 0 | −10.59 | 0.7667 | 0.5782 |
| airspy | 18 | 20 | −26.82 | 0.5620 | 0.5782 |
| airspy | 36 | 0 | −10.79 | 0.8092 | 0.6072 |
| airspy | 36 | 20 | −26.07 | 0.5873 | 0.6072 |

At w0 even `-E18`/`-E36` carry a persistent ~33% excess of off-reference tap
energy and sit ~10 dB from the baseline solution indefinitely. It is far below
the level that shows up in the audio (−60 dBFS, §14) and does not change Part
I's practical verdict, but "no lasting difference" was an audio-domain
statement, not a filter-state one. w20 removes most of this too.

---

## 16. Pilot PLL

`DEBUG_PLL_FILTER` frequency trace, one sample per block. The loop limits
`m_freq` to 19000 ± 30 Hz (`include/PilotPhaseLock.h:35`,
`sfmbase/PilotPhaseLock.cpp:132`); a sample within 1 µHz of either rail counts
as railed.

| file | `-E` | w | std 0–1 s | 1–2 s | 2–5 s | 5–10 s | railed 0–5 s | last railed |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| airspy | 36 | 0 | 3.976 | 0.034 | 0.034 | 0.028 | 1/938 (0.1%) | 0.01 s |
| airspy | 36 | 20 | 3.166 | 0.033 | 0.034 | 0.027 | 0/938 | — |
| airspy | 36 | 50 | 3.127 | 0.042 | 0.035 | 0.027 | 0/938 | — |
| airspy | 36 | 100 | 3.291 | 0.074 | 0.036 | 0.027 | 0/938 | — |
| airspy | 100 | 0 | **20.803** | **19.115** | **19.562** | **8.326** | **89/938 (9.5%)** | **5.89 s** |
| airspy | 100 | 20 | 2.314 | 0.041 | 0.042 | 0.034 | 0/938 | — |
| airspy | 100 | 50 | 2.309 | 0.051 | 0.043 | 0.034 | 0/938 | — |
| airspy | 100 | 100 | 2.415 | 0.092 | 0.044 | 0.034 | 0/938 | — |
| interfm | 100 | 0 | **19.553** | **20.435** | **19.775** | **19.969** | **76/938 (8.1%)** | **29.84 s** |
| interfm | 100 | 20 | 2.109 | 0.081 | 0.047 | 0.044 | 0/938 | — |
| interfm | 100 | 50 | 2.124 | 0.090 | 0.047 | 0.044 | 0/938 | — |
| interfm | 100 | 100 | 2.156 | 0.121 | 0.047 | 0.044 | 0/938 | — |

(Frequency standard deviations in Hz.)

w20 and w50 **never rail the limiter**, at any stage count — matching w100 and
eliminating the multi-second open-loop excursion that w0 causes at `-E100`.
Their 1–2 s jitter is in fact marginally *lower* than w100's (0.041 vs 0.092 Hz
at `-E100`; 0.033 vs 0.074 Hz at `-E36`), because the filter engages while the
loop is still acquiring rather than stepping an already-settled loop.

The engagement kick that §9 measured for w100 is reduced but not removed:

| `-E` | w20 | w50 | w100 |
| --- | --- | --- | --- |
| 36 | +18.11 Hz @ 0.107 s | +17.01 Hz @ 0.267 s | +19.87 Hz @ 0.533 s |
| 100 | +5.29 Hz @ 0.107 s | +4.81 Hz @ 0.267 s | +7.24 Hz @ 0.533 s |

A shorter wait therefore delivers a **smaller** kick, **earlier**, while the
loop is still in acquisition and its own jitter (2–4 Hz) is an order of
magnitude larger than the kick's effect on the settled loop. This is the
mechanism by which w20 gets the best of both endpoints.

---

## 17. Where the onset is: waits of 5, 10, and 15 blocks

w20 works and w0 does not, so the failure onset lies in between. Bracketing it
at `-E100` on the three recordings that fail worst:

| file | w | after-1 s (dBFS) | correlation | off-ref @end | w100 off-ref | peak output |
| --- | --- | --- | --- | --- | --- | --- |
| airspy | 0 | −15.48 | 0.275904 | **3.7529** | 0.6826 | 2.800 |
| airspy | 5 | −65.40 | 0.998717 | 0.7815 | 0.6826 | 1.981 |
| airspy | 10 | −72.13 | 0.998784 | 0.7093 | 0.6826 | 1.609 |
| airspy | 15 | −73.78 | 0.998827 | 0.6674 | 0.6826 | 1.440 |
| airspy | 20 | −73.21 | 0.998839 | 0.6628 | 0.6826 | 1.440 |
| airspy | 50 | −75.94 | 0.999111 | 0.6671 | 0.6826 | 1.496 |
| interfm | 0 | −22.39 | 0.888168 | **3.1812** | 0.4978 | 2.738 |
| interfm | 5 | −64.22 | 0.997955 | 0.5646 | 0.4978 | 1.960 |
| interfm | 10 | −71.14 | 0.998041 | 0.5231 | 0.4978 | 1.556 |
| interfm | 15 | −77.25 | 0.998119 | 0.5077 | 0.4978 | 1.535 |
| interfm | 20 | −79.32 | 0.998179 | 0.5046 | 0.4978 | 1.487 |
| interfm | 50 | −82.31 | 0.998684 | 0.4947 | 0.4978 | 1.463 |
| piano | 0 | −16.19 | −0.235228 | **2.0170** | 0.0104 | 1.732 |
| piano | 5 | −36.29 | 0.979705 | **2.0107** | 0.0104 | 2.290 |
| piano | 10 | −92.76 | 0.993285 | 0.0104 | 0.0104 | 1.334 |
| piano | 15 | −93.61 | 0.993375 | 0.0104 | 0.0104 | 1.090 |
| piano | 20 | −93.84 | 0.993460 | 0.0104 | 0.0104 | 1.061 |
| piano | 50 | −96.49 | 0.995704 | 0.0104 | 0.0104 | 1.071 |

**The onset is not a single sharp threshold — it is file-dependent, and 5
blocks is on the wrong side of it.**

On piano the transition between 5 and 10 blocks is a cliff: w5's final tap
state (off-ref 2.0107) is indistinguishable from w0's (2.0170) — still the
wrong solution, 193× the baseline — and its audio, though 20 dB better than
w0, is still **56 dB worse** than w10's. On airspy and interfm, by contrast, w5
has already recovered most of the deficit (after-1 s −65 and −64 dBFS,
correlation 0.998), yet still carries 13–14% excess off-reference tap energy
and has not converged to the baseline solution.

What w5 does do on *all three* is stress the filter: peak output 1.96–2.29
against baselines of 1.06–1.46, the three highest values measured anywhere in
this evaluation apart from w0's own `-E100` runs. By w10 every file is within
4–5% of the baseline off-reference energy and peak output is back under 1.61;
by w15–w20 all three are within 3% and peak output is at the baseline.

So the last value that fails outright is **5** (on the worst file), the first
value that is clean everywhere is **10–15**, and from 15 onward the metrics are
flat: w15 → w20 → w50 moves the after-1 s figure by 1–5 dB and the tap state
not at all.

### Why 10 blocks is enough, and what it says about the mechanism

The IF AGC's gain ramp is nearly identical on all four recordings that need one
(gain as a fraction of its 2–5 s median):

| file | K | b0 | b5 | b10 | b15 | b20 | b30 | b50 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| airspy | 13.04 | 0.077 | 0.210 | 0.517 | 0.883 | 1.015 | 1.000 | 1.026 |
| interfm | 13.03 | 0.077 | 0.208 | 0.511 | 0.861 | 0.953 | 0.986 | 0.994 |
| joakfm | 11.59 | 0.086 | 0.234 | 0.559 | 0.879 | 0.980 | 0.993 | 0.992 |
| piano | 11.31 | 0.088 | 0.240 | 0.565 | 0.883 | 0.979 | 0.991 | 0.985 |

(The AGC runs upstream of the wait check — `FmDecode.cpp:107` precedes
`:109` — so this trajectory is identical for every wait value, as the traces
confirm.)

The filter survives engagement at **~52–57% of target gain** (block 10) and is
at best marginal at **~21–24%** (block 5). So the requirement is *not* that the
AGC be settled, as §11 assumed when it proposed gating on AGC convergence: the
CMA error at block 10 is still ≈ 1 − 0.55² ≈ 0.70, far from zero, and the loop
converges correctly anyway. What matters is being past the steepest part of the
ramp — between blocks 5 and 10 the gain more than doubles, a rate of change
comparable to the filter's own adaptation time constant, and that is the region
the filter cannot track.

**A caveat on the mechanism.** §8 attributed the `-E100` pathology to the
interaction of a large tap count with the AGC's gain ramp, on the evidence that
a K ≈ 1 file converged cleanly. Round 2 shows the AGC ramp is **necessary but
not sufficient**: joakfm has the same ramp (K = 11.59, identical trajectory)
yet survives w0 at `-E100` almost unscathed (correlation 0.9962, off-ref 0.0173
against a 0.0107 baseline), while piano — same ramp, K = 11.31 — is the worst
failure measured. Whatever else distinguishes them (signal content, noise
floor, absence of real echo structure) was not isolated here, and §8's causal
test used only one K ≈ 1 file. The ramp is confirmed as a precondition; it is
not by itself a predictor of severity.

---

## 18. Revised recommendation (supersedes §11)

**Set the wait to 20 blocks.** On the evidence of the full 4 × 3 × 7 grid this
is the value that dominates both endpoints Part I tested:

- It removes the `-E100` failure that made removal unsafe: every `-E100` metric
  at w20 — audio (−73 to −106 dBFS after 1 s), coefficients (`reldb` −29 to
  −68 dB, off-ref within 3% of baseline), PLL (zero railed blocks) — is at the
  w100 baseline, on all seven recordings.
- It captures nearly all of the benefit Part I identified for removal: the
  engagement kick shrinks (+18.1 Hz vs +19.9 Hz at `-E36`; +5.3 Hz vs +7.2 Hz
  at `-E100`) and lands at 0.107 s, inside the PLL's own acquisition transient,
  where 1–2 s jitter is actually *lower* than with the 100-block wait.
- It cuts the wait to a fifth: 0.107 s on a file source or Airspy HF+, 0.131 s
  on an Airspy R2 at 10 Msps, 0.284 s on an RTL-SDR at 1.152 Msps (§2's
  arithmetic, scaled by 20/100).
- It is not marginal. The last wait that fails outright is 5 blocks and the
  first that is clean on every recording is 10–15 (§17), so 20 carries a 4×
  margin over the former and ~1.5× over the latter, and w15/w20/w50 are
  indistinguishable in the tap domain.

**Do not go below 10 blocks**, and prefer 20 over 10 to keep that margin. At 5
blocks the worst recording still misconverges as badly as at 0, and all three
bracketed recordings show their highest peak filter output of the entire
evaluation (1.96–2.29 against baselines of 1.06–1.46).

**On the AGC-gating idea of §11**: §17 shows the trigger condition is weaker
than "AGC converged" — the filter is safe at ~55% of target gain — so gating on
convergence would wait longer than necessary while adding state and a threshold
to tune. A fixed 20-block count is simpler and is now directly validated. The
one property gating would still buy is automatic adaptation to a source whose
AGC ramps unusually slowly; note that the ramp measured here is almost
identical across four recordings and two amplitude regimes, so that scenario
remains hypothetical on the available evidence.

### What is still not established

- **All measurements are through `filesource`.** No hardware source was
  tested. §2's per-device wait durations remain arithmetic, and an SDR's IQ
  level at power-on could differ from a recording's, changing K and hence
  where the 5–10 block onset sits. The 20-block recommendation inherits this
  caveat; a live-hardware check at `-E100` is the obvious validation step.
- **The onset stage count is still unbracketed.** Only `-E18`, `-E36` and
  `-E100` were tested, in both rounds. With w20 the stage-count dependence
  disappears, so this matters much less than it did for §11's verdict, but it
  is still unmeasured.
- **The 5→10 block onset was bracketed at `-E100` only**, on three recordings.
  Whether it moves with stage count was not tested.
- **Severity is not predicted by the AGC ramp alone** (§17), and what else
  drives it was not isolated.
- Nothing here revisits `doc/MULTIPATH_FILTER_DESIGN_20260724.md`'s finding
  that the filter is a net loss on fading channels regardless of this wait.

---

## 19. Round-2 reproduction

```sh
git worktree add --detach /tmp/mfwait2 dev
cp -R r8brain-free-src /tmp/mfwait2/ && rm -f /tmp/mfwait2/r8brain-free-src/.git
# The harness is doc/MULTIPATH_QMM_20260813_harness.diff: it initializes
# m_wait_multipath_blocks from a once-parsed getenv("MF_WAIT_BLOCKS"), echoes
# MFWAIT,<n> to stderr, and adds the DEBUG_MF_RESET / DEBUG_MF_ERR /
# DEBUG_AGC_TRACE prints of §1.1 (DEBUG_MF_ERR now carries the true
# FmDecoder::process() block index).
git -C /tmp/mfwait2 apply doc/MULTIPATH_QMM_20260813_harness.diff

cmake -S /tmp/mfwait2 -B /tmp/mfwait2/build-dbg \
  -DEXTRA_FLAGS="-DCOEFF_MONITOR=1 -DDEBUG_MULTIPATH_FILTER=1 -DDEBUG_MF_RESET=1 -DDEBUG_MF_ERR=1 -DDEBUG_AGC_TRACE=1"
cmake -S /tmp/mfwait2 -B /tmp/mfwait2/build-pll -DEXTRA_FLAGS="-DDEBUG_PLL_FILTER=1"
cmake --build /tmp/mfwait2/build-dbg --target airspy-fmradion -j
cmake --build /tmp/mfwait2/build-pll --target airspy-fmradion -j
```

The saved harness defaults the wait to 20 rather than the 100 this round's
build used, since it was regenerated against the tree after the change of
`de9dc0c`. That does not affect anything below: every decode sets
`MF_WAIT_BLOCKS` explicitly, and each run's log records the value actually
used as `MFWAIT,<n>`.

Each decode, per §1.2's file/frequency table:

```sh
MF_WAIT_BLOCKS=<0|20|50|100> /tmp/mfwait2/build-dbg/airspy-fmradion \
  -m fm -t filesource -E <18|36|100> \
  -c freq=<freq>,srate=384000,filename=test-files/<file>,wav,format=FLOAT \
  -q -G out/<tag>_w<wait>_e<stages>.wav 2> logs/<tag>_w<wait>_e<stages>.log
```

Run 12-way concurrent (`xargs -P 12`), longest recordings first. **Then verify
the run was lossless before trusting any of it:**

```sh
grep -l "queue overflow" logs/*.log | wc -l   # must be 0
grep -h "^EXIT:" logs/*.log | sort | uniq -c  # must be all EXIT:0
```

Metrics are computed by `analyze_audio.py` (whole/after-1s/windowed difference,
correlation, M/S split), `analyze_logs.py` (resets, peak output, off-reference
energy, `reldb` trajectory) and `analyze_pll.py` (frequency std, railed-block
fraction, engagement step) against the w100 decode of the same recording and
stage count.
