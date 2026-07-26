# End-to-end audio delay vs the `-L` option, measured against a reference receiver (20260725)

**Date:** 2026-07-25
**Author:** Claude Code (claude-opus-5)
**Binary:** 20260716-0, git SHA1 `37bec3c` (branch `dev`, reported "with
uncommitted changes") — the installed `~/bin/airspy-fmradion`, inferred
from `condition.md` invoking it by bare name; that commit carries the
revised `MultipathFilter` but not the VOLK FIR low-pass rewrite.
macOS 26.5.2 (Darwin 25.5.0), Mac mini 2023
**Recordings:** `wrk-delay-test/joak-L{3,5,10,20,30,40}.wav`, conditions in
`wrk-delay-test/condition.md`
**Scripts:** `doc/measure_lr_delay.py`, `doc/loopback_clock_cal.py`
**Figure:** `doc/LATENCY_MEASUREMENT_20260725_fig.png`

## Executive summary

The delay measured here is the **complete end-to-end delay** — antenna to
analog audio output — obtained by recording an analog FM radio tuned to the
same station on one channel and the airspy-fmradion output on the other, and
measuring the lag between them. Unlike
`LATENCY_CHECK_20260719.md`, which measured only the output stage
(decoder block pull → audio at the device) with a real-time-paced
FileSource, this measurement includes the Airspy HF+ capture, the whole
DSP chain, PortAudio/CoreAudio, and the FiiO K7 DAC, and it is driven by
a live SDR.

| `-L` | Granted by CoreAudio | **Measured end-to-end delay** | Stability within the run |
|---|---|---|---|
| 3 | 13.667 ms | **34 → 46 ms, never settled** | grows +1.11 ms/s |
| 5 | 26.333 ms | **37.37 ms** | ±0.06 ms |
| 10 | 52.667 ms | **54.36 ms** | ±0.02 ms |
| 20 | 105.333 ms | **91.63 ms** | ±0.02 ms |
| 30 | 200.667 ms | **132.59 ms** | ±0.02 ms |
| 40 = default | 210.667 ms | **151.15 ms** | ±0.02 ms |

`-L 40` is identical to the shipped default (`minimum_latency_default =
0.04`), so **151 ms is the default configuration's end-to-end delay** on
this host, and `-L 10` cuts it to about a third of that.

Key findings:

- **The deep buffers never fill on a live SDR.** For `-L ≥ 20` the
  delivered delay is far *below* the granted capacity (0.87, 0.66 and
  0.72 of it) — the opposite of the FileSource-driven ladder of
  `LATENCY_CHECK_20260719.md` §16, where the delivered figure always
  *exceeded* the grant. Delivered latency is a ring-buffer *fill level*
  set by start-up history, and an Airspy HF+ feeding samples in true real
  time never produces the start-up burst that fills a deep buffer. The
  practical consequence: the earlier ladder over-states what a real
  receiver session delivers at large `-L`, by 60-85 ms at the top.
- **`-L 3` does not reach a steady state.** The delay climbs
  monotonically at +1.11 ± 0.11 ms/s (34.1 ms at the start, 45.7 ms
  11 s later) and had not settled when the 12.6 s recording ended. Before
  t = 7 s it has already passed `-L 5`. `-L 3` is not a usable setting on
  this host.
- **`-L 5` and `-L 10` are the useful low-latency points**, at 37 and
  54 ms; `-L 10` costs 17 ms over `-L 5` and, per
  `LATENCY_CHECK_20260719.md` §13.1, buys a much larger dropout margin.
- **A slow upward creep is present at every setting**: +3.6 to +4.1 ppm
  at `-L 20/30/40`, i.e. about **13 ms per hour** of listening. This is
  the un-disciplined clock difference between the Airspy HF+ sample rate
  and the K7's DAC clock accumulating in the output buffer
  (`LATENCY_PLAN_20260713.md` §7.5), now visible as an absolute delay
  drift rather than as an internal fill-level number.
- **Clock calibration confirmed the Rubix24 is −189.54 ppm** and that
  this matters for essentially nothing: it inflates the measured delays
  by 0.007-0.029 ms. It is applied anyway, and the same loopback run
  also bounds the FiiO K7's own hardware delay at **≤ 1.32 ms**.

## 1. What was measured, and why this differs from the earlier work

> **End-to-end delay** = the wall-clock time from the arrival of an RF
> waveform at the antenna to the appearance of the corresponding audio at
> the analog output of the DAC.

The reference is a second, analog receiver listening to the same
broadcast, whose own delay is negligible (§6.3). Both receivers' analog
outputs enter the same ADC, so everything on the capture side — ADC
latency, USB transport, driver buffering — is common to both channels and
cancels exactly in the lag between them.

This is a strictly larger quantity than the "output-stage latency" of
`LATENCY_CHECK_20260719.md`, which begins at the decoder's block pull. It
adds the Airspy HF+ device and USB batching and the group delay of the
whole FM decode chain, and it drops nothing.

## 2. Setup

From `wrk-delay-test/condition.md`, recorded by the maintainer:

```
Recording device: Roland Rubix24 with Audacity, 44.1 kHz 16-bit PCM WAV
Left  channel:    Sony ICF-P37 analog radio output
Right channel:    airspy-fmradion output via FiiO K7, NO SoundSource

airspy-fmradion -m fm -L 40 -E100 -t airspyhf -c freq=82500000 -P -
```

Only `-L` changed between files (82.5 MHz = NHK JOAK-FM Tokyo). Six
recordings, 12.6-19.3 s each, one run per `-L` value. SoundSource was not
in the audio path, so the ~44 ms ACE constant documented in
`LATENCY_CHECK_20260719.md` §15/§17 does **not** apply to any figure here.

## 3. Sample-clock calibration (FiiO K7 → Rubix24 loopback)

The Rubix24 does not run at exactly 44 100 Hz, so a lag measured in
recorded samples is not a time until its clock is known. Calibration was
performed on the same physical cabling as the measurement — the K7's
output is already wired to the Rubix24's right input — by
`doc/loopback_clock_cal.py`: a 60 s, 3 kHz tone at −20 dBFS played out of
the K7 at 48 kHz (the rate airspy-fmradion uses) and recorded on the
Rubix24 at its native 44.1 kHz, with 10 ms marker chirps at 5 s + k·10 s.

Two independent estimators were computed.

| Quantity | Method | Result |
|---|---|---|
| Rubix24 ADC rate | linear fit of 8 905 PortAudio `inputBufferAdcTime` blocks vs frame index | **44 091.641 Hz = −189.54 ppm**, residual 0.40 frames rms |
| FiiO K7 DAC rate | linear fit of 41 104 `outputBufferDacTime` blocks | **47 999.648 Hz = −7.33 ppm**, residual 0.46 frames rms |
| K7/Rubix ratio | from the two fits above | +182.251 ppm |
| K7/Rubix ratio | phase slope of the received 3 kHz tone over 59.8 s | +182.277 ppm (3000.546830 Hz; phase residual 0.0102 rad rms) |

The two ratio estimates agree to **0.026 ppm**, which is the strongest
available evidence that both are right. The −189.54 ppm figure also
matches the −190 ppm noted in `condition.md` and independently measured
in `LATENCY_CHECK_20260719.md` §14.1 by an entirely different method
(matched-filter stretch search), and the K7's −7.33 ppm is inside that
section's −10 to −15 ppm estimate.

**Effect on the results:** delays are converted with 44 091.641 Hz
instead of 44 100 Hz, which increases them by 189.5 ppm — from +0.007 ms
at `-L 5` to +0.029 ms at `-L 40`. The calibration is therefore not
what decides any conclusion here; it is what allows that to be *stated*
rather than assumed. Both columns are given in §5.

Note also what does *not* need correcting. Both channels of every
recording share one ADC clock, so the L/R lag is on a single timebase and
no relative stretch has to be removed — unlike the earlier work, where a
decode on the host clock had to be aligned against a recording on the
Rubix clock.

### 3.1 By-product: the analog path delay

The marker chirps give the delay from the K7's PortAudio DAC timestamp to
the Rubix24's PortAudio ADC timestamp: **1.316 ms**, with a standard
deviation of 0.0004 ms across six chirps spanning 50 s. Since PortAudio's
`outputBufferDacTime`/`inputBufferAdcTime` are defined at the converter,
this 1.316 ms is everything the two devices do *beyond* what PortAudio
accounts for — K7 interpolation filter and USB transport, cable, Rubix24
anti-alias filter and USB transport. It bounds the K7's own unaccounted
contribution to the measured delays at **≤ 1.32 ms**, and its
run-to-run constancy also confirms both clock fits are drift-free over a
minute.

## 4. Delay measurement method

`doc/measure_lr_delay.py`, per recording:

1. Band-pass both channels to 200-4000 Hz (4th-order Butterworth,
   zero-phase). The band is where the mono program content of the two
   receivers is most alike; the analog radio rolls off above ~5 kHz and
   the stereo difference signal in the airspy-fmradion channel is
   uncorrelated with it.
2. Coarse lag from a full-length FFT cross-correlation over 0-1 s.
3. Track the lag in 2.0 s windows every 0.25 s, searching ±30 ms around
   the coarse lag, refined by parabolic interpolation of the correlation
   peak.
4. Keep windows with normalized correlation > 0.5 and within 25 ms of the
   median; report median, spread and a linear drift fit.

Alignment quality: the median normalized correlation is 0.99+ for
`-L 10/20/30/40` and 0.70-0.74 for `-L 3/5` (quieter program passages in
those two recordings). The peaks are unambiguous — over a −0.5 to +2 s search the
strongest competing peak, separated by at least 10 ms from the main one,
reaches 0.05-0.29 of it (0.53 for `-L 3`, the shortest and noisiest
recording). Windowed residuals about the linear drift fit are **0.002 ms rms**
(0.09 samples) for `-L 20/30/40`.

Direction convention: the lag is that of the airspy-fmradion channel
relative to the radio channel, so it is positive and increasing means the
software receiver is falling further behind.

## 5. Results

Granted values are `Pa_GetStreamInfo()->outputLatency` for the FiiO K7 at
48 kHz, measured directly with a blocking-mode stream opened exactly as
`AudioOutput.cpp` opens it (no callback, `paFramesPerBufferUnspecified`).
They reproduce the grant law of `LATENCY_CHECK_20260719.md` §9.2 exactly:
granted = B + R, R the request in frames, B the smallest power of two
≥ 3R.

| `-L` | Granted | Delivered (calibrated) | Delivered (uncalibrated) | 48 kHz frames | Delivered − granted | Delivered / granted |
|---|---|---|---|---|---|---|
| 3 | 13.667 ms | 36.45 ms (median, drifting) | 36.44 ms | 1750 | +22.78 ms | 2.67 |
| 5 | 26.333 ms | **37.37 ms** | 37.37 ms | 1794 | +11.04 ms | 1.42 |
| 10 | 52.667 ms | **54.36 ms** | 54.35 ms | 2609 | +1.69 ms | 1.03 |
| 20 | 105.333 ms | **91.63 ms** | 91.62 ms | 4398 | −13.70 ms | 0.87 |
| 30 | 200.667 ms | **132.59 ms** | 132.57 ms | 6364 | −68.07 ms | 0.66 |
| 40 | 210.667 ms | **151.15 ms** | 151.12 ms | 7255 | −59.52 ms | 0.72 |

Within-run behavior:

| `-L` | Windows used | Min-max | Drift |
|---|---|---|---|
| 3 | 34/43 | 34.10-45.74 ms | **+1.1101 ± 0.1071 ms/s** |
| 5 | 69/69 | 37.27-37.48 ms | +0.0109 ± 0.0009 ms/s (+10.9 ppm) |
| 10 | 57/57 | 54.31-54.41 ms | +0.0016 ± 0.0005 ms/s (+1.6 ppm) |
| 20 | 65/65 | 91.60-91.66 ms | +0.0041 ± 0.0001 ms/s (+4.1 ppm) |
| 30 | 67/67 | 132.56-132.62 ms | +0.0036 ± 0.0000 ms/s (+3.6 ppm) |
| 40 | 53/53 | 151.12-151.16 ms | +0.0036 ± 0.0001 ms/s (+3.6 ppm) |

Left panel of the figure plots all six tracks; the right panel plots
requested, granted and delivered against `-L`.

## 6. Interpretation

### 6.1 Requested, granted, delivered — a third time, with a different answer

`LATENCY_CHECK_20260719.md` established that the CoreAudio *grant* is
several times the request, and that the *delivered* latency is the ring
buffer's fill level rather than its capacity. With a real-time-paced
FileSource, that fill level always ended up at or above capacity
(delivered − granted was +18 to +33 ms at every point of the clean
ladder, §16.1).

With a live Airspy HF+ the picture inverts above `-L 10`:

| `-L` | This work, end-to-end, airspyhf | `LATENCY_CHECK` §16, output stage only, FileSource |
|---|---|---|
| 5 | 37.4 ms | 47.1 ms (loopback) / 31.4 ms (K7, §15) |
| 10 | 54.4 ms | 77.0 ms |
| 20 | 91.6 ms | 132.9 ms |
| 30 | 132.6 ms | 233.6 ms |
| 40 (default) | 151.1 ms | 236.7 ms |

The right-hand column measures *less* of the signal path and yet reports
*more* delay at every point from `-L 10` up. The mechanism is start-up
history: a blocking-write producer fills the ring only as fast as its
source supplies samples, and an SDR supplies them in true real time, so a
deep ring is never filled. A FileSource, even when real-time paced, can
hand over a burst while the output stream is still starting. The deeper
the buffer, the more room there is for the two behaviors to diverge —
hence 0.87, 0.66, 0.72 of capacity at `-L 20/30/40` versus over 1.0
throughout the earlier ladder.

The corollary is that **`-L 30` and `-L 40` are not distinguished by
their capacities** (200.667 vs 210.667 ms, only 10 ms apart) but delivered
132.6 vs 151.1 ms, 18.6 ms apart. Nothing deterministic connects the
grant to the delivered figure in that regime; it is set by whatever
occupancy the process happened to reach at start-up.

### 6.2 What the fixed part of the delay is

At `-L 5` the buffer is small enough that both experiments should be
running it near capacity, which makes it the one point where the two
measurement definitions can be differenced:

```
37.37 ms  this work: source + decode chain + output stage + K7 hardware
31.4  ms  LATENCY_CHECK §15: block pull -> Rubix24 ADC timestamp, K7, clean
--------
 5.97 ms  difference
```

The two definitions differ on both ends. The earlier figure starts at the
decoder's block pull, so it omits the Airspy HF+ device and the decode
chain's group delay — call that `S` — and it ends at the Rubix24's
PortAudio ADC timestamp, so it *includes* whatever part of §3.1's
1.316 ms belongs to the capture side, which our L/R lag cancels. Hence
`S = 5.97 ms + (Rubix24 unaccounted input delay)`, and since that term
lies between 0 and 1.32 ms:

> **Airspy HF+ device and USB batching plus the entire decode chain —
> IF filtering, the `-E100` multipath filter, discriminator, stereo
> decode, pilot-cut FIR, de-emphasis, audio resampler — costs about
> 6.0-7.3 ms.**

That is comfortably consistent with `LATENCY_PLAN_20260713.md`'s budget
for an Airspy HF+ at 384 kHz (~5 ms native batching, 1.31 ms pilot-cut
FIR, ~1.5 ms audio resampler, no IF resampler).

This is a difference of two experiments run six days apart with different
sources, so treat 6.0-7.3 ms as an estimate with a couple of ms of slack,
not as a measurement. It is nonetheless the useful message: **on this
hardware the software receive chain is a small part of the delay, and the
output stage is nearly all of it.**

### 6.3 The reference receiver's own delay

The Sony ICF-P37 is an analog superheterodyne portable. Its FM path delay
is dominated by the 10.7 MHz IF ceramic filter (a few microseconds of
group delay) and the audio stage; the total is well under 0.5 ms. Both
receivers are at the same location, so the propagation difference is
nanoseconds. The reference delay enters with a negative sign, so every
figure in §5 is if anything a slight **under**-estimate, by less than
0.5 ms.

### 6.4 `-L 3`: no steady state

`-L 3` is the only setting whose delay does not settle. It starts at
34.1 ms — already 20 ms above its 13.667 ms grant — and climbs at
1.11 ms/s, reaching 45.7 ms after 11 s with no sign of leveling off,
crossing `-L 5`'s delay just before t = 7 s. The climb is smooth on a 2 s
window and resolves into 0.5-1.5 ms steps on a 0.5 s window; it is not the
10.67 ms host-buffer quantum an ordinary output underrun would insert, so
the excess is more likely accumulating on the input side — a main loop
that, when made to write in very small pieces, averages slightly slower
than real time and lets the source queue grow. That mechanism is inferred
from the step size, not measured here.

Whatever the mechanism, the behavior is decisive: at `-L 3` the delay
grows without bound over a recording, so the setting is worse than `-L 5`
in both latency and stability. Note that `main.cpp` accepts `-L` down to
1 ms.

### 6.5 The 3.6-4.1 ppm creep

The three cleanest recordings agree closely: `-L 20` +4.08 ± 0.06 ppm,
`-L 30` +3.57 ± 0.05 ppm, `-L 40` +3.64 ± 0.08 ppm, with residuals of
2 µs rms about the fitted line. The airspy-fmradion output therefore
falls behind real time by roughly **13 ms per hour**.

This is the visible face of the missing clock discipline noted in
`LATENCY_PLAN_20260713.md` §7.5: the audio sample rate is derived from
the Airspy HF+'s crystal, the DAC drains at the K7's crystal (measured
here at −7.33 ppm against the host), and nothing reconciles the two, so
the difference accumulates in the output buffer. At `-L 40` the ~60 ms of
unused capacity would absorb about 4.5 hours of creep. At `-L 5` the
delivered delay is already 11 ms *above* the grant, so there is no
measured headroom at all, and that recording drifts faster (+10.9 ppm) —
with no room in the ring the surplus has to accumulate somewhere else,
presumably the input queue. Long unattended sessions at low `-L` are the
case worth watching.

## 7. Uncertainty

| Term | Size | Nature |
|---|---|---|
| Window-to-window scatter, `-L ≥ 10` | 0.002-0.02 ms rms | random, averaged over 53-67 windows |
| Window-to-window scatter, `-L 5` | 0.04 ms rms | random |
| Sub-sample peak interpolation | ~0.02 ms | systematic, common to all points |
| Rubix24 clock, after calibration | < 0.001 ms | negligible |
| Rubix24 clock, if uncalibrated | 0.007-0.029 ms | systematic, one-sided |
| Reference receiver delay | < 0.5 ms | systematic, one-sided (delays are under-estimates) |
| **Single run per `-L`** | unknown; tens of ms possible at `-L ≥ 20` | **dominant** |

Everything within a run is known to about ±0.1 ms. The dominant
uncertainty is that each `-L` was recorded **once**. Since delivered
latency is a start-up-history-dependent fill level (§6.1), and the
earlier work saw fill states differing by tens of milliseconds between
runs of the same configuration (`LATENCY_CHECK_20260719.md` §3, §8.1,
§14.2), the figures for `-L ≥ 20` should be read as *one observed state
of that configuration*, not as its expected value. The `-L 5` and `-L 10`
points, where the buffer is small and runs near capacity, should be far
more reproducible. Repeating the ladder three times would settle this and
is the obvious next step if the deep-`-L` numbers are to be quoted.

## 8. Practical reading

- The default (`-L 40`, or no `-L`) delivers **151 ms** end-to-end here.
- **`-L 10` → 54 ms** is the recommended operating point: about a third
  of the default, with the dropout margin that `-L 8`-`-L 10` was shown
  to have in `LATENCY_CHECK_20260719.md` §13.1.
- **`-L 5` → 37 ms** for minimum latency, accepting the near-zero fill
  margin (§13.1 of the earlier work recorded audible gaps under host load
  in the 1024-frame bucket).
- **Do not use `-L 3`**, nor anything else in the 512-frame grant bucket
  (`-L ≤ 3` on this host). It is not a steady-state configuration.
- `-L 20` and `-L 30` have no niche: `-L 20` costs 37 ms over `-L 10`,
  and `-L 30` costs 41 ms more again, for no measured robustness gain.
- Roughly, between `-L 5` and `-L 30` each millisecond of `-L` buys or
  costs **3.4-4.1 ms** of real delay. Above `-L 30` the return collapses
  to 1.9 ms per millisecond, because `-L 30` and `-L 40` share a host
  buffer.

## 9. Reproduction

```sh
# 1. Clock calibration (K7 output already cabled to the Rubix24 input)
python3 doc/loopback_clock_cal.py --seconds 60 --base cal

# 2. Delay measurement, using the ADC rate from step 1
python3 doc/measure_lr_delay.py --fs-true 44091.641 wrk-delay-test/*.wav

# 3. Granted CoreAudio latencies for the ladder
python3 - <<'EOF'
import sounddevice as sd
for L in (3, 5, 10, 20, 30, 40):
    s = sd.RawOutputStream(device='FiiO K7 ', samplerate=48000, channels=2,
                           dtype='float32', blocksize=0, latency=L/1000.0)
    s.start(); print(L, s.latency * 1000); s.abort(); s.close()
EOF
```

To re-record the ladder, repeat the `condition.md` setup and vary only
`-L`; three runs per point, cycled rather than grouped, would remove the
§7 caveat.
