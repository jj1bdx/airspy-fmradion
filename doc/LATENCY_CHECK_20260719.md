# Sampled PortAudio output latency: default vs `-L 5` (20260719)

**Date:** 2026-07-19
**Author:** Claude Code (claude-fable-5)
**Binary:** 20260716-0 (`dbca134`), macOS (Darwin 25.5.0), Mac mini
**Scope:** Direct measurement of the *actual* PortAudio output latency
difference between the default output latency and `-L 5`, obtained by
sampling the real PortAudio output stream (not by trusting requested or
reported values). FM stereo decode of a 384 kHz IQ file
(`test-files/piano_iqtest.wav`, 20 s, float32 IQ) through the real-time-paced
FileSource.

## Executive summary

Measured end-to-end output-stage latency (decoder block ready → audio
sampled at the output device), mean over three (default: four) runs each:

| Configuration | Suggested | Granted by CoreAudio | Sampled actual latency |
|---|---|---|---|
| default (no `-L`) | 40 ms (floor) | 210.667 ms | **267.4 ms** (settled) |
| `-L 5` | 5 ms | 26.333 ms | **75.6 ms** |

- **Difference: 191.7 ms.** `-L 5` removes about 72% of the output-stage
  latency relative to the default on this host.
- Run-to-run repeatability is ±0.4 ms; within-run drift ≤ 0.3 ms.
- Two of the four default runs spent their first ~10 s in a *lower* state
  of 238.8 ms, then stepped up by exactly **+1360 output frames
  (28.33 ms)** to the settled 267.4 ms state, where they stayed. No `-L 5`
  run ever changed state. See §4.
- The requested→granted inflation on this CoreAudio host is large and
  matches the 2026-07-14 FiiO K7 measurement exactly (40 ms request →
  210.667 ms granted on both devices): it is a property of PortAudio's
  CoreAudio host implementation, not of the particular output device.
  The sampled actual latency then sits another ~50-57 ms above the
  granted figure (blocking-write ring buffer occupancy plus the
  measurement loop path, §5).
- **Addendum §8 (same day):** the same method applied to `-L 20` vs
  `-L 10` gives 161.2 ms vs 104.3 ms sampled actual. `-L 10` was as
  deterministic as `-L 5`; `-L 20` is already in the history-dependent
  regime (observed states 129.6-162.4 ms).
- **Addendum §9 (same day):** `-L 15` sampled 152.0 ms — almost as slow
  as `-L 20`, because the CoreAudio grant is *not* linear: it is an
  internal power-of-2 host buffer plus the request
  (granted = 2^ceil(log2(3R)) + R frames), with a cliff right at
  14→15 ms (granted 56.7 → 100.3 ms). The apparent ×5.2667 linearity
  of §8.3 was a sampling artifact of probing only 5/10/20/40 ms.
- **Addendum §10 (same day):** `-L 14` sampled 108.8 ms, confirming
  §9.3's ≈108 ms prediction — one requested millisecond across the
  14→15 cliff costs ~43 ms of delivered latency. The underrun-recovery
  step quantum is ~28.33 ms in *every* bucket, so it is not the
  power-of-2 host buffer (refines §4.1's wording).

## 1. What was measured

The latency defined and measured here is the **output-stage latency**:

> the wall-clock delay from the moment the main loop has pulled the IQ
> block that produces a given decoded audio sample (immediately before
> decoding and the blocking `Pa_WriteStream()` call), to the moment that
> sample appears in the PortAudio output stream of the device.

It therefore *includes* the per-block decode compute time (~1 ms) and the
whole PortAudio/CoreAudio output buffering, and *excludes* the DSP group
delays of the decode chain itself (those were measured separately in
`LATENCY_PLAN_20260713.md` §9-§11 and are identical in both
configurations here anyway).

## 2. Method

Playback goes to a loopback audio device and is recorded back, so the
actual output samples are observed on a common clock:

```
piano_iqtest.wav (384 kHz IQ, 20 s)
    → airspy-fmradion -t filesource -P <Loopback 1> [-L 5] -T pps.txt
    → Loopback 1 virtual device (Rogue Amoeba), 48 kHz
    → Python/PortAudio capture stream (sounddevice), float32,
      with per-callback ADC timestamps
```

Three anchors make the numbers absolute, not just relative:

1. **Decoder timeline → unix clock:** the `-T` PPS markers. Each PPS line
   holds a 384 kHz baseband `sample_index` and a unix timestamp
   interpolated from the main-loop block pull times (`main.cpp`,
   "Write PPS markers"). A linear fit of the 19 PPS events per run gives
   the emit time of any output frame `f` (baseband time `f`/48000).
   Fit residuals: 0.5-1.8 ms std; fitted slope 1.00000 ± 0.00006
   (FileSource real-time pacing confirmed).
2. **Recording index → unix clock:** PortAudio input-callback ADC
   timestamps, linear fit residual < 0.001 ms, PortAudio-to-unix clock
   offset measured by paired reads (spread < 0.1 ms).
3. **Recording ↔ decoded content:** full-length FFT cross-correlation of
   the mono-folded recording against a `-W` reference decode of the same
   file. Because the decode chain is deterministic, the correct lag gives
   a **sample-exact** match: correlation coefficient 1.000000 and a
   residual at −82 dB (the S16 quantization floor of the reference) for
   every clean run. Alignment is therefore verified, not assumed.

Latency per run is `arrival(f) − emit(f)` averaged over
`f ∈ [3 s, 18 s]` (evaluated every 10 ms). Runs alternated
default/`-L 5` to decorrelate any host drift; streams and processes were
restarted for every run.

## 3. Per-run results

| Run | Suggested | Settled latency | Early state (before t≈10 s) |
|---|---|---|---|
| default_1 | 40 ms | 267.2 ms | 238.9 ms (+28.33 ms step at ≈10 s) |
| default_2 | 40 ms | 267.6 ms | — (settled from start) |
| default_3 | 40 ms | 267.7 ms | — (settled from start) |
| default_4 | 40 ms | 267.0 ms | 238.6 ms (+28.33 ms step at ≈10 s) |
| l5_1 | 5 ms | 75.5 ms | — |
| l5_2 | 5 ms | 75.6 ms | — |
| l5_3 | 5 ms | 75.9 ms | — |

Settled means: default 267.4 ms (266.97-267.73), `-L 5` 75.6 ms
(75.48-75.88). **Difference of settled states: 191.7 ms.** Against the
early default state the difference is 163.1 ms.

## 4. The 28.33 ms state step in default runs

A windowed (0.5 s) correlation scan of every recording tracked the
alignment lag over time. In `default_1` and `default_4` the lag is
constant and sample-exact up to t≈10 s, then jumps by exactly **+1360
frames (28.333 ms)** in both runs, and is constant and sample-exact
afterwards. `default_2`/`default_3` show the settled lag from the first
window (0.5 s) onward; all `-L 5` runs are single-lag throughout. The
capture-side timeline is provably gapless (ADC-timestamp fit residual
< 1 µs), so the step happened on the playback side: the audio content
was delayed by one additional 28.33 ms quantum mid-stream.

Consistent interpretation: with the huge granted buffer of the default
configuration, the blocking-write stream can start with its ring buffer
not yet at final occupancy; an early underrun-recovery tops it up by one
host-buffer quantum, after which the occupancy — and the latency — stays
at the settled value. The default early state sits exactly one quantum
(28.2 ms ≈ 28.33 ms) above the granted 210.667 ms. The practical reading:
the default configuration's latency is not even stable run-to-run within
its first seconds, while `-L 5` was rock-solid in every run.

### 4.1 Interpretation: does this mean the default is too low, or too high?

Neither "too low" in the underrun sense — the data says the default is,
if anything, too high for its stated purpose, and the instability is a
symptom of the buffer being large, not small.

**Mechanism.** With the blocking-write API, the latency heard is set by
the *fill level* of the output ring buffer, not just its capacity. The
decoder writes at exactly 1x real time (FileSource pacing; an SDR source
is the same), so it can never actively fill a buffer deeper than
whatever level the startup transient left it at. The default run gets a
granted capacity of 210.667 ms, and where within that capacity the fill
level lands is essentially historical accident: the PortAudio stream
starts in the constructor and underruns on silence until the first
decoded block arrives, and each later underrun-recovery inserts one
host-buffer quantum (28.33 ms) of silence, permanently raising the fill
level — that is the observed step. Two runs took that step at t≈10 s;
the other two evidently took it during the startup silence gap and were
already settled by the first measurable window. Once at the higher
level there is more scheduling margin, so no further steps occurred.

**Why this points at "too high" rather than "too low".** A buffer that
underruns once in 10 s on a virtual loopback device is not starved —
the render thread hiccupped once, and with a deep buffer the
*consequence* of a hiccup is a permanent +28 ms rather than a click.
The deep buffer does not prevent the glitch; it converts it into
silently accumulated latency. So the default configuration yields a
latency that (a) is ~6.7x its nominal 40 ms once CoreAudio's grant
inflation is applied, and (b) is not even a single number — it is
"somewhere between 239 and 267 ms depending on how many recovery events
have happened so far". The small granted buffer of `-L 5` (26.3 ms)
fills to capacity immediately and pins the fill level: every `-L 5` run
measured 75.6 ± 0.2 ms from the first half-second to the end.

**Caveat before concluding the default should shrink.** The 40 ms floor
exists for dropout robustness on loaded or slower machines, and this
experiment cannot rule that need out: a 20-second decode into a virtual
device on an idle Mac mini is a gentle workload, and the same underrun
that cost +28 ms of latency here would have been an audible dropout had
the buffer been near-empty instead. What the measurement establishes is
that the default's nominal value badly understates what is actually
delivered (40 ms requested → ~267 ms sampled), and that its delivered
latency is history-dependent rather than deterministic. If low latency
matters, `-L` is the right tool and behaves predictably; lowering the
*default* would be justified only after checking dropout behavior under
realistic load on the slowest supported hosts (longer runs, CPU
contention, physical DAC) — a separate experiment.

## 5. Reconciliation: suggested vs granted vs sampled

Cross-checked by opening an independent PortAudio output stream on the
same device and reading `Pa_GetStreamInfo()->outputLatency`:

| | default | `-L 5` |
|---|---|---|
| suggestedLatency (printed by binary) | 0.040 s | 0.005 s |
| granted `outputLatency` | 210.667 ms | 26.333 ms |
| sampled actual (settled) | 267.4 ms | 75.6 ms |
| sampled − granted | 56.7 ms | 49.3 ms |

The granted value for a 40 ms request is bit-identical to the value
measured on the FiiO K7 USB DAC on 2026-07-14 (210.667 ms), confirming
the request inflation is generic to the PortAudio CoreAudio host. The
sampled−granted excess (~50-57 ms) is the part `Pa_GetStreamInfo()`
does not report: blocking-API ring-buffer occupancy plus the constant
measurement-path terms (decode compute, loopback pass-through, capture
input path). Those constants are identical in both configurations and
cancel in the 191.7 ms difference.

## 6. Validity notes

- The output device is a virtual loopback, so the absolute figures are
  loopback-path figures; a physical DAC adds its own conversion/transport
  constants. The granted-latency identity with the FiiO K7 indicates the
  *difference* transfers to physical devices, but the absolute numbers
  should not be quoted as DAC figures.
- The reference decode (`-W`) and all playback runs used the same binary
  and file; sample-exact correlation (§2) rules out any content
  ambiguity in the alignment, including octave/period errors of the
  correlation peak.
- `default_1`'s and `default_4`'s settled-state numbers are taken from
  their post-step segments; their early-state numbers are the settled
  value minus the content-verified 1360-frame step.
- Measurement floor: all anchor uncertainties combined are ≈ 1-2 ms,
  two orders of magnitude below the measured difference.

## 7. Reproduction

```sh
# reference decode (real-time paced, ~20 s)
airspy-fmradion -t filesource \
  -c filename=test-files/piano_iqtest.wav -W reference.wav

# measured runs (Loopback 1 was PortAudio device 6; check the
# "playing audio to PortAudio device N: name '...'" stderr line)
airspy-fmradion -t filesource \
  -c filename=test-files/piano_iqtest.wav -P 6 -T pps.txt        # default
airspy-fmradion -t filesource \
  -c filename=test-files/piano_iqtest.wav -P 6 -T pps.txt -L 5   # -L 5
```

While each run plays, a Python `sounddevice` (PortAudio) input stream on
the same loopback device records 48 kHz float32 audio and logs
per-callback `inputBufferAdcTime` plus PortAudio-to-unix clock pairs.
Analysis: linear-fit the PPS file (emit clock), linear-fit the ADC
timestamps (arrival clock), FFT-cross-correlate the recording against
`reference.wav` (alignment, verified sample-exact), and average
`arrival − emit` over t = 3-18 s.

## 8. Addendum (same day): `-L 20` vs `-L 10`

Same method, binary, IQ file, and loopback device as §1-§7, measured
the same day. Four `-L 20` runs and three `-L 10` runs, alternated; a
fourth `-L 20` run was added after two of the first three changed
state mid-run. Every stable segment of every run aligned sample-exact
against the reference decode (correlation 1.000000, residual −82 dB).

### 8.1 Results

| Configuration | Suggested | Granted by CoreAudio | Sampled actual latency |
|---|---|---|---|
| `-L 20` | 20 ms | 105.333 ms | **161.2 ms** (capacity state, §8.2) |
| `-L 10` | 10 ms | 52.667 ms | **104.3 ms** (stable) |

- **Difference of the capacity states: 56.9 ms** (granted difference:
  52.7 ms).
- `-L 10` behaved like `-L 5`: every run held a single state from the
  first 0.5 s window to the end (103.4 / 104.5 / 104.9 ms; within-run
  std ≤ 0.7 ms).
- `-L 20` did **not** deliver a single latency: three of four runs
  changed state mid-run, in *both* directions.

Per-run states:

| Run | State trajectory |
|---|---|
| l20_1 | 133.1 ms until t≈10 s, then +28.33 ms step → 160.0 ms |
| l20_2 | 162.4 ms until t≈3 s, then −32.8 ms drop → 129.6 ms |
| l20_3 | 161.3 ms throughout |
| l20_4 | 133.3 ms until t≈10 s, then +28.33 ms step → 161.0 ms |
| l10_1 | 103.4 ms throughout |
| l10_2 | 104.5 ms throughout |
| l10_3 | 104.9 ms throughout |

The capacity state (buffer at full occupancy) is 161.2 ms
(160.0-162.4 across the four `-L 20` runs); the one-quantum-low state
is 133.2 ms, exactly one 28.33 ms quantum below it.

### 8.2 Fill-level wander in both directions — §4.1 confirmed

- **Up-steps:** `l20_1` and `l20_4` show the identical +1360-output-
  frame (28.333 ms) underrun-recovery quantum as the default runs of
  §4, and in the same 9.5-10.0 s window. Their early state sits
  exactly one quantum below capacity.
- **Down-step:** `l20_2`'s PPS anchor shows its first three markers on
  a line 32.8 ms of emit-time *earlier* than the steady line of the
  remaining sixteen (which fit with 0.4 ms residual, slope 0.999993):
  the producer stalled once for ~33 ms at t≈3 s. The ring drained by
  the stall duration and stayed there — latency dropped permanently
  from 162.4 to 129.6 ms. The recording is gapless and sample-exact at
  a single content lag throughout: the deep buffer absorbed the stall
  without any audible dropout, at the price of a silently shifted
  latency.
- Together these are the mirror-image confirmation of §4.1's
  mechanism: the fill level moves up by one host-buffer quantum on an
  underrun recovery and down by the stall length on a producer stall;
  capacity only bounds it. A granted buffer of 105.3 ms (`-L 20`) is
  large enough to wander; 52.7 ms (`-L 10`) and 26.3 ms (`-L 5`)
  filled to capacity immediately and never moved in any run. On this
  host the boundary of the history-dependent regime therefore lies
  between granted 52.7 ms and 105.3 ms.
- All four observed up-steps across both experiments (default and
  `-L 20`) occurred at t≈10 s after stream start, which suggests a
  deterministic ~10 s trigger on this host (CoreAudio or the loopback
  driver's housekeeping) rather than random scheduling noise.

### 8.3 The grant is exactly linear on this host

**[Superseded by §9.2 the same day.]** The linearity below is a
sampling artifact: 5/10/20/40 ms all double together, landing at the
same relative position in the host's power-of-2 buffer buckets. The
true grant law is bucket + request and is strongly non-linear between
buckets (e.g., 14 ms → 56.7 ms but 15 ms → 100.3 ms).

Granted `outputLatency` for an independent output stream on the same
device, measured in one session:

| Requested | Granted | Ratio |
|---|---|---|
| 40 ms | 210.667 ms | 5.267 |
| 20 ms | 105.333 ms | 5.267 |
| 10 ms | 52.667 ms | 5.267 |
| 5 ms | 26.333 ms | 5.267 |

The CoreAudio-host inflation is a constant ×5.2667 multiplier over the
whole tested range, not a fixed offset or a floor.

### 8.4 The full ladder

| Configuration | Granted | Sampled (capacity state) | Sampled − granted |
|---|---|---|---|
| default (40 ms floor) | 210.667 ms | 267.4 ms | 56.7 ms |
| `-L 20` | 105.333 ms | 161.2 ms | 55.9 ms |
| `-L 10` | 52.667 ms | 104.3 ms | 51.6 ms |
| `-L 5` | 26.333 ms | 75.6 ms | 49.3 ms |

Sampled actual latency tracks the granted value plus the ~50-57 ms
occupancy-and-measurement-path constant of §5, growing mildly with
buffer size.

### 8.5 Practical reading

`-L 10` delivers ~104 ms actual output-stage latency on this host and
was exactly as deterministic as `-L 5`. `-L 20` already sits in the
history-dependent regime: its delivered latency was "somewhere between
129.6 and 162.4 ms depending on run history". If predictable latency
matters, choose `-L 10` or lower on this host. The validity notes of
§6 apply unchanged (loopback-path absolutes; the difference transfers
to physical devices). Reproduction: the §7 commands with `-L 20` /
`-L 10` in place of the latency options shown there.

## 9. Addendum (same day): `-L 15`, and the real CoreAudio grant law

Same method as everything above; three `-L 15` runs. This experiment
was expected to probe the wander boundary between granted 52.7 ms
(`-L 10`, pinned) and 105.3 ms (`-L 20`, wanders) — the presumed-linear
grant for 15 ms would be 79.0 ms. The granted value actually measured
is **100.333 ms**, which falsified the linear model of §8.3 and led to
the grant-law sweep in §9.2.

### 9.1 `-L 15` results

| Configuration | Suggested | Granted by CoreAudio | Sampled actual latency |
|---|---|---|---|
| `-L 15` | 15 ms | 100.333 ms | **152.0 ms** (mean of 3 runs) |

| Run | Behavior |
|---|---|
| l15_1 | 148.9 ms mean; drifted 159 → 139 ms over the run |
| l15_2 | 151.8 ms mean; mild drift 154 → 149 ms |
| l15_3 | 155.4 ms; stable |

Every run stayed at a single, sample-exact content lag (no
playback-side underrun steps). The drift in l15_1/l15_2 is
producer-side: their PPS anchors show the block-pull timeline running
slightly slow (l15_1 slope 1.0013, i.e. ~0.13% under real time), so
the ring gradually drained — ~20 ms over 15 s in l15_1 — without ever
underrunning. This is the same fill-level physics as §8.2, in slow
motion: a buffer this deep absorbs producer pacing wobble by silently
trading fill level (= latency) instead of dropping out. The mean
152.0 ms equals granted 100.3 ms + 51.7 ms, in line with the §8.4
ladder.

### 9.2 The real grant law: power-of-2 host buffer + request

Sweeping the granted `outputLatency` over requests of 1-40 ms on the
same device (one session, independent output stream):

| Request | Granted | Granted (frames) | = host buffer + request |
|---|---|---|---|
| 1 ms | 6.333 ms | 304 | 256 + 48 |
| 2 ms | 12.667 ms | 608 | 512 + 96 |
| 3 ms | 13.667 ms | 656 | 512 + 144 |
| 4 ms | 25.333 ms | 1216 | 1024 + 192 |
| 5 ms | 26.333 ms | 1264 | 1024 + 240 |
| 7 ms | 28.333 ms | 1360 | 1024 + 336 |
| 8 ms | 50.667 ms | 2432 | 2048 + 384 |
| 10 ms | 52.667 ms | 2528 | 2048 + 480 |
| 14 ms | 56.667 ms | 2720 | 2048 + 672 |
| **15 ms** | **100.333 ms** | 4816 | 4096 + 720 |
| 20 ms | 105.333 ms | 5056 | 4096 + 960 |
| 28 ms | 113.333 ms | 5440 | 4096 + 1344 |
| **29 ms** | **199.667 ms** | 9584 | 8192 + 1392 |
| 40 ms | 210.667 ms | 10112 | 8192 + 1920 |

Every measured point fits one rule: with the request R in frames at
48 kHz,

> granted = B + R, where B is the smallest power of two ≥ 3·R.

The host buffer B doubles at requests of ~2.67 · 2^n / 3 frames, i.e.
the granted latency has *cliffs* between 14→15 ms and 28→29 ms (and
7→8 ms, 3→4 ms, …). Within a bucket, granted grows only 1 ms per
requested ms. The §8.3 "×5.2667 linear" observation is what this
function looks like when sampled only at 5/10/20/40 ms: doubling the
request doubles both terms, keeping the ratio (B+R)/R constant.

### 9.3 Practical reading

- `-L 15` is a poor operating point on this host: it requests 25% less
  than `-L 20` but lands in the same 4096-frame bucket, delivering
  152.0 vs 161.2 ms — only ~9 ms better — and its deep buffer again
  shows history-dependent fill-level behavior (drift instead of
  steps this time).
- The efficient choices sit at the top of each bucket, just below the
  cliffs: 14 ms (granted 56.7 ms, predicted ≈ 108 ms sampled — not
  measured here), 7 ms (granted 28.3 ms), or the measured `-L 10` /
  `-L 5` points. Between `-L 14` and `-L 15` one requested millisecond
  costs 43.7 ms of granted latency.
- The updated ladder (sampled actual, capacity state): default 267.4,
  `-L 20` 161.2, `-L 15` 152.0, `-L 10` 104.3, `-L 5` 75.6 ms.

## 10. Addendum (same day): `-L 14` — the §9.3 prediction tested

Three `-L 14` runs, same method. §9.3 predicted ≈108 ms sampled from
the grant law (granted 56.667 ms + the ~52 ms excess). Measured:

| Configuration | Suggested | Granted by CoreAudio | Sampled actual latency |
|---|---|---|---|
| `-L 14` | 14 ms | 56.667 ms | **108.8 ms** (mean of 3 runs) |

| Run | Behavior |
|---|---|
| l14_1 | 80.8 ms until t≈10 s, then +28.35 ms step → 109.1 ms |
| l14_2 | 111.6 ms; stable throughout |
| l14_3 | drifted 114.4 → 96.9 ms (slow-producer drain, as l15_1); mean 105.7 ms |

All runs sample-exact at their stable lags. Findings:

- **Prediction confirmed.** 108.8 ms measured vs ≈108 ms predicted.
  The sampled ≈ granted + ~52 ms model of §8.4 now holds at six
  operating points spanning granted 26.3-210.7 ms.
- **The recovery quantum is not the host buffer.** l14_1's up-step is
  +1361 output frames (28.35 ms) — the same ~28.33 ms quantum as the
  default runs (§4) and the `-L 20` runs (§8.2), even though the three
  configurations use host buffers of 8192, 4096, and 2048 frames
  (170.7, 85.3, 42.7 ms). §4.1 called this insert "one host-buffer
  quantum"; that attribution was wrong — the quantum is
  bucket-independent, so it belongs to something further down the
  chain (the loopback device driver's cycle, or a fixed
  recovery-path insert), not to the PortAudio host buffer size.
- **Fifth up-step, same timing.** Every underrun-recovery up-step
  observed today (default ×2, `-L 20` ×2, `-L 14` ×1) occurred at
  t≈10 s after stream start, strengthening the deterministic-trigger
  reading of §8.2.
- **`-L 14` also wanders.** With granted 56.7 ms, 2 of 3 runs moved
  (one step up from a quantum-low start, one slow drain). `-L 10`
  (granted 52.7 ms) never moved in 3 runs — but the two sit in the
  same 2048-frame bucket only 4 ms apart, so this contrast is more
  likely small-sample luck than a sharp boundary; treat "granted
  ≲ 50 ms pins, ≳ 100 ms wanders" as the robust reading, with the
  50-100 ms range only lightly probed.

Practical reading: `-L 14` delivers ~109 ms vs `-L 10`'s ~104 ms,
matching their 4 ms granted difference — within a bucket, delivered
latency moves 1 ms per requested ms, so the lowest request in a bucket
wins (e.g., 8 ms → granted 50.7 ms, the bottom of the 2048 bucket,
slightly below `-L 10`). Requesting across a cliff is what matters:
`-L 14` → 108.8 ms but `-L 15` → 152.0 ms.

The full measured ladder (sampled actual, capacity state): default
267.4, `-L 20` 161.2, `-L 15` 152.0, `-L 14` 108.8, `-L 10` 104.3,
`-L 5` 75.6 ms.
