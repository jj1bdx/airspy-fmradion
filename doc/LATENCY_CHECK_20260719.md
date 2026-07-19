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
  regime (observed states 129.6-162.4 ms). The CoreAudio grant is an
  exactly linear ×5.2667 of the request over the whole 5-40 ms range.

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
