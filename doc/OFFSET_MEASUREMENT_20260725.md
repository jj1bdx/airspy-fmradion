# Measuring audio-interface sample-clock offsets on macOS

**Date:** 2026-07-25
**Scripts:** `doc/clock_offset_measure.py`, `doc/clock_offset_analyze.py`,
`doc/coreaudio_rate.py`
**Platform:** macOS (Apple Silicon and Intel), Python 3.11+

These tools measure how far an audio interface's sample clock actually
sits from its nominal rate, in parts per million, for both the DAC
(output) and the ADC (input) side, at 44.1, 48 and 96 kHz.

They generalise the one-off calibration in
[`LATENCY_MEASUREMENT_20260725.md`](LATENCY_MEASUREMENT_20260725.md) §3,
which pinned the Rubix24 at −189.54 ppm so that a delay counted in
recorded samples could be converted into a time. That script
(`loopback_clock_cal.py`) was hard-wired to one device pair and one rate
pair; this one sweeps rates, takes either device by number, and — the
part that turned out to matter most — refuses to report a number when
macOS has quietly put a resampler in the path.

---

## 1. What is measured, and against what

A sample clock has nothing to compare itself to. Every offset here is a
*ratio* between two oscillators, so the tools always name the reference.

| Symbol | Meaning |
|---|---|
| `p_dac` | output device's DAC clock offset, ppm, against the Mac's host clock |
| `p_adc` | input device's ADC clock offset, ppm, against the Mac's host clock |

Three estimators are computed, and the redundancy is the point.

| | What it fits | Reference |
|---|---|---|
| **E1** | PortAudio `outputBufferDacTime` vs cumulative frame index | host clock |
| **E2** | PortAudio `inputBufferAdcTime` vs cumulative frame index | host clock |
| **E3** | phase slope of the recorded tone | the other device |

E1 and E2 read the host clock; E3 never does. E3 measures
`p_dac − p_adc`: a tone synthesised at `f0` relative to the DAC's nominal
rate leaves the DAC at `f0·(1+d)`, and measuring it against the ADC's
nominal rate divides by `(1+a)`, giving `d − a` to first order.

Three measurements of two unknowns leaves one spare degree of freedom.
That spare degree is the whole reason to bother: E1/E2 come from
PortAudio's callback timestamps and E3 from the recorded waveform's own
sample-indexed timeline, so they share no noise source, and a
disagreement between them is real evidence that something is wrong.

### 1.1 The host clock is not UTC

PortAudio's CoreAudio timestamps live in the `mach_absolute_time`
domain, which free-runs on the Mac's own crystal; only `CLOCK_REALTIME`
is steered towards UTC by NTP. E1 and E2 are therefore offsets against
*that crystal*, not against absolute time.

The tool samples `time.monotonic()` and `time.time()` throughout each run
and fits the two against each other by total least squares, then reports
both figures. On the test Mac mini the monotonic clock runs
−3.3 to −3.4 ppm against NTP-disciplined time, consistently across runs.

The correction **subtracts**. If one monotonic second is `(1 + c·1e-6)`
real seconds, the monotonic clock under-reports elapsed time, so a rate
computed per monotonic second is overstated by `c` ppm:

```
p_utc = p_host − c
```

Use the "vs host clock" number when what you care about is drift against
the machine — buffer fill in a long-running decode, which is
`LATENCY_MEASUREMENT_20260725.md` §6.5's +3.6 to +4.1 ppm creep. Use the
UTC-corrected number when comparing crystals across machines or days.

---

## 2. The trap: macOS resamples without telling you

**This is the single thing that makes the measurement wrong if ignored,
and it is invisible from inside PortAudio.**

PortAudio will open a stream at any rate a CoreAudio device claims to
accept. When the requested rate is not the device's *current nominal
rate*, macOS inserts an `AudioConverter` and the stream runs at the
requested rate while the hardware keeps running at its own. The fitted
offset then describes the resampler, not the clock.

Measured on the test machine, a Roland Rubix24 whose hardware supports
only 44.1/48/96/192 kHz. 88.2 kHz is not in the default sweep, but it is
the cleanest demonstration of the trap, because this device cannot
produce it under any circumstances:

| Guard | Result |
|---|---|
| `sd.check_input_settings(samplerate=88200)` | **passes** — on hardware that cannot do 88.2 kHz at all |
| `stream.samplerate` after opening | reads back **88200.0** |
| CoreAudio `kAudioDevicePropertyNominalSampleRate` | still **44100** |

So neither the PortAudio settings probe nor the opened stream's own
`samplerate` attribute detects it. Only asking CoreAudio does.

What the difference costs, same device, 60 s runs:

| Requested | Device left at 44.1 kHz (resampled) | Device actually switched |
|---|---|---|
| 48 kHz | −192.3 ppm, residual **89.5** frames rms | −179.1 ppm, residual **0.34** frames rms |
| 96 kHz | −187.7 ppm, residual **436** frames rms | −178.8 ppm, residual **0.36** frames rms |

(E2 alone, so that the offsets and residuals come from the same fit.)

The resampled numbers are not merely noisier, they are *wrong by 10 ppm*:
they report the hardware's 44.1 kHz clock leaking through the converter,
which is why they all cluster near −190 ppm regardless of the rate asked
for. And on the output side the giveaway is subtler still — a resampled
FiiO K7 gave −7.6 ppm with a perfectly clean 0.29-frame residual, versus
−13.0 ppm when actually switched. **Residual size does not reliably
detect resampling.** Query CoreAudio.

`coreaudio_rate.py` does that: it reads each device's real
`AudioDeviceID`, nominal rate and hardware rate list, sets the nominal
rate before opening the stream, verifies the change took effect, and
restores the original rate afterwards. Rates the hardware does not
support are skipped with an explicit message rather than silently faked.

---

## 3. Hardware setup

One cable, from the output device's line output to the input device's
line input.

```
   ┌──────────────┐                      ┌──────────────┐
   │ output device│  line out ────────▶  │ input device │
   │  (e.g. K7)   │      analog cable    │ (e.g. Rubix) │
   └──────────────┘                      └──────────────┘
          ▲                                      │
          └──────── same Mac, two USB ports ─────┘
```

Requirements:

- **Two physically separate interfaces.** The tool refuses
  `--out-device == --in-device`: a device looping back to itself shares
  one oscillator and reads 0 ppm by construction.
- **Not an aggregate or virtual device.** Aggregate devices, Loopback,
  BlackHole and similar derive their clock from a host timer, and macOS
  applies its own drift correction across an aggregate's members, so you
  would measure the residual after correction rather than the clocks.
  The tool flags these by name and requires `--allow-virtual`.
- **Level.** Anything from −40 to −6 dBFS at the ADC is fine. A line
  output feeding a line input through a volume control is routinely
  20–30 dB down and this costs the estimator nothing; only
  signal-to-noise matters, and the report prints it. Avoid clipping.
- **Quiet machine.** Close other applications using either interface.
  CoreAudio devices are shared, not exclusive, and another client can
  change the device's nominal rate or buffer size mid-run.

The analog path's frequency response, delay and even a moderate amount
of hum are all irrelevant — E3 measures a *frequency ratio*, and a fixed
delay is a constant phase offset that a slope fit ignores.

---

## 4. Installation

```sh
pip install sounddevice numpy      # soundfile only for --save-audio
```

`sounddevice` needs PortAudio (`brew install portaudio`).

**Microphone permission.** The first `InputStream.start()` triggers a TCC
prompt attributed to the *terminal application*, not to the script. Grant
it in System Settings → Privacy & Security → Microphone. If it is denied,
or if there is no GUI session to show the dialog (`ssh`, `cron`,
`launchd`), macOS may hand back **silent all-zero input with no error at
all** — which is why the tool checks the recorded level and refuses to
fit a silent recording. A TCC grant is tied to the interpreter binary, so
switching between system, Homebrew and venv Pythons can re-prompt.

---

## 5. Usage

### 5.1 Find the device numbers

```sh
python3 doc/clock_offset_measure.py --list-devices
```

```
idx  name                            in out  current   hardware rates
------------------------------------------------------------------------
  0  EV2785                           0   2  44100     32000,44100,48000,88200,96000,176400,192000
  1  FiiO K7                          0   2  44100     44100,48000,88200,96000,176400,192000,352800,384000
  2  Rubix24                          2   4  44100     44100,48000,96000,192000
  3  Mac mini Speakers                0   2  48000     44100,48000,88200,96000
  4  rekordbox Aggregate Device       0   2  44100     44100,48000,88200,96000,... *
  5  WSJTX Input                      2   2  44100     44100,48000,88200,96000,... *
  6  Loopback 1                       2   2  48000     44100,48000,88200,96000,... *
  7  K7 speaker / Rubix HP            2   6  44100     44100,48000,88200,96000,... *
```

These are **PortAudio device indices** — the same numbering
airspy-fmradion's `-P` option takes. They are not CoreAudio
`AudioDeviceID`s; run `python3 doc/coreaudio_rate.py` if you want those.
The `hardware rates` column is what the hardware genuinely supports;
note the Rubix24 has no 88.2 kHz.

### 5.2 Measure

```sh
python3 doc/clock_offset_measure.py --out-device 1 --in-device 2 --seconds 60
```

Roughly five minutes for the default four-rate sweep. A tone is audible
on the output device throughout.

### 5.3 Re-analyse without the hardware

Every capture is saved as an `.npz`, so the estimators can be re-run
later, on any machine:

```sh
python3 doc/clock_offset_analyze.py clock_offset_data/capture_*.npz
```

### 5.4 Options

| Option | Default | Notes |
|---|---|---|
| `--out-device N` | required | PortAudio index to play from |
| `--in-device N` | required | PortAudio index to record on |
| `--rates` | `44100,48000,96000` | comma-separated; unsupported rates are skipped |
| `--seconds` | `60` | per rate; see §7 for what this buys |
| `--amplitude` | `0.1` | −20 dBFS |
| `--latency` | `low` | `low`, `high`, or seconds |
| `--blocksize` | `0` | 0 = host default, which is the least glitch-prone |
| `--in-channel` | loudest | which recorded channel to analyse |
| `--out-dir` | `clock_offset_data` | where captures are written |
| `--save-audio` | off | also write the recording as a float WAV |
| `--allow-virtual` | off | permit aggregate/virtual devices |
| `--keep-rate` | off | leave devices switched instead of restoring |

---

## 6. Worked example

FiiO K7 output → Roland Rubix24 input, Mac mini, 60 s per rate.

```
      rate             FiiO K7 (out)              Rubix24 (in)
     44100           -7.222 +/- 0.008          -190.278 +/- 0.008
     48000          -12.230 +/- 0.219          -179.130 +/- 0.220
     96000          -12.115 +/- 0.080          -178.946 +/- 0.080
```

Three things are worth reading off this table.

**Both offsets are rate-dependent, in the same direction.** The K7 sits
at −7.2 ppm at 44.1 kHz but −12.1 ppm on the 48 kHz family; the Rubix24
at −190.3 ppm versus −179.0 ppm. Those gaps — 5 ppm and 11 ppm — dwarf
the error bars. Each interface synthesises the two families with a
different PLL divider, and the two dividers do not land in the same
place. **A calibration taken at one rate does not transfer to the
other**, which is the practical result of this whole exercise.

**The 48 and 96 kHz figures agree within their error bars** on both
devices — 0.12 ppm apart on the K7, 0.18 ppm on the Rubix24 — as they
should, since those two rates come off the same PLL family and divider
chain. Nobody designed that check in; it falls out of the sweep.

**The 44.1 kHz Rubix24 figure reproduces prior work.** This tool gives
−190.28 ppm; `LATENCY_MEASUREMENT_20260725.md` §3 measured −189.54 ppm by
fitting `inputBufferAdcTime` on a different day, and
`LATENCY_CHECK_20260719.md` §14.1 got ≈ −190 ppm by matched-filter
stretch search, an entirely different method. Three methods inside
0.8 ppm, which is about the size of the day-to-day thermal drift in
§6.1.

It also corrects one number in the earlier document. That −7.33 ppm for
the "K7 DAC at 48 kHz" was taken with the K7 left at 44.1 kHz nominal
while 48 kHz was requested — a resampled measurement, per §2. The K7's
true 48 kHz clock is about −12.1 ppm. This does not change any conclusion
there: that work only needed the *Rubix* calibration to convert recorded
samples to time, and the K7 figure appeared as a cross-check.

### 6.1 Reproducibility

Three independent 60 s runs at 48 kHz on the same pair, spread over about
an hour, all re-analysed with the same code:

| Run | K7 output | Rubix24 input | quoted error | Birge ratio |
|---|---|---|---|---|
| A | −12.105 ppm | −179.030 ppm | ±0.014 | 2.1 |
| B | −11.992 ppm | −178.912 ppm | ±0.118 | 17.5 |
| C | −12.230 ppm | −179.130 ppm | ±0.219 | 32.3 |
| **spread (sd)** | **0.119 ppm** | **0.109 ppm** | | |

The observed run-to-run scatter, 0.11 ppm, sits comfortably inside the
error bars the tool quotes for a typical run. That is the evidence that
the uncertainty model in §7.2 is honest — and note that the raw internal
scatter of these fits was 0.004 to 0.007 ppm, some twenty times smaller
than what the runs actually reproduce. Without the Birge scaling the tool
would be claiming a precision it does not have.

The Birge ratio varying from 2 to 32 across otherwise identical runs is
itself informative: the systematic difference between the timestamp and
tone estimators is not a fixed property of the setup but something that
moves with conditions, which is exactly why it is folded into the error
bar rather than subtracted out as a calibration.

**This does not bound drift over longer intervals.** Comparing the two
full sweeps taken about an hour apart, every figure moved coherently on
both devices — 44.1 kHz by 0.35–0.42 ppm, 48 kHz by 0.22–0.24 ppm,
96 kHz by 0.07–0.09 ppm — every one of the six in the same direction. Crystals warm
up and rooms change temperature. Treat the error bar as the precision of
a single measurement, not as a guarantee that the interface will read the
same tomorrow; if you need a calibration constant to hold, re-measure it
under the conditions you intend to use it in.

---

## 7. Interpreting the report

### 7.1 Per-rate detail

```
  Individual estimators
    E1 DAC vs host     44099.6815 Hz     -7.222 +/- 0.001 ppm   40437 blocks  resid   0.034 frames rms (max 0.3)
    E2 ADC vs host     44091.6005 Hz   -190.464 +/- 0.014 ppm    8710 blocks  resid   0.325 frames rms (max 0.7)
    E3 DAC/ADC tone   2940.538186 Hz (nominal 2940.0)   +183.056 +/- 0.000 ppm    5975 blocks  phase resid 0.00038 rad rms
       recorded tone: -9.2 dBFS, effective SNR 39.1 dB
    (residuals are serially correlated; error bars are HAC-inflated by up to 3.5x)
```

- **`resid ... frames rms`** is the health check on E1/E2. Under about
  1 frame rms is a properly switched device. Tens or hundreds of frames
  means a resampler is in the path (§2).
- **`phase resid ... rad rms`** is the health check on E3. Thousandths of
  a radian is good; a value near 1 radian means the phase fit found no
  coherent tone.
- **`effective SNR`** is derived from the phase scatter, so it also
  absorbs slow phase wander and reads pessimistically low. Below about
  10 dB, distrust E3.

### 7.2 The combined estimate and the Birge ratio

```
  Combined (weighted least squares, 3 measurements / 2 unknowns)
    FiiO K7 output            -7.222 +/- 0.008 ppm  (44099.6815 Hz)
    Rubix24 input           -190.278 +/- 0.008 ppm  (44091.6087 Hz)
    chi2 = 174.30 (1 dof), p = 0.000, Birge ratio 13.2  -> CONSISTENT
    quoted error is the internal 0.0006 ppm scaled by the Birge ratio;
    estimator residuals [ 3.641e-04 -1.861e-01 -3.603e-06] ppm
```

The internal scatter of a 60 s fit reaches about 0.004 ppm. The
estimators nevertheless disagree with each other by 0.02 to 0.4 ppm,
because each carries systematics its own residuals cannot see —
timestamp granularity on E1/E2, the analog path and room on E3.

Quoting 0.004 ppm would be a lie, since §6.1 shows the measurement does
not reproduce to better than ~0.12 ppm. So the uncertainty is scaled by
the **Birge ratio** `sqrt(chi²/dof)`, the standard metrology treatment of
discrepant data, and the scaled figure is what the summary table reports.
The ratio is never allowed below 1: agreeing better than chance is not
evidence of extra precision.

Consequently `chi2` and `p` will look catastrophic on a perfectly good
run and should be read as *descriptions of the spread*, not as a verdict.
The verdict is the **`CONSISTENT` / `DISCREPANT`** label, which triggers
on the estimators disagreeing by more than **1 ppm** — the threshold that
separates "different systematics" from "something broke".

### 7.3 Warnings that matter

| Message | Meaning |
|---|---|
| `CoreAudio is resampling` | the rate could not be set; the numbers describe a resampler (§2) |
| `does not support it in hardware` | rate skipped, correctly |
| `callbacks reported over/underflow` | a glitch occurred; check the frame residuals |
| `recorded level is ... below` | below −40 dB relative to the tone; check cabling |
| `the estimators disagree by more than 1 ppm` | distrust that rate |

---

## 8. Method notes

Points where the implementation departs from the obvious approach, each
because the obvious approach is measurably wrong.

**Regress time on frames, not frames on time.** The frame counter is
exact and the timestamp is the noisy quantity, so time-on-frames is the
unbiased orientation. Regressing the other way — treating the noisy
timestamp as the exact regressor, as `loopback_clock_cal.py` does —
attenuates the slope by roughly `var(noise)/var(t)`, some 0.0003 to
0.03 ppm here. Small, but free to eliminate.

**Centre both axes.** Timestamps are seconds since an arbitrary epoch and
reach 10⁴ s, whose squares lose about six decimal digits to cancellation
against a 60 s span. Centring keeps every summed product `O(span²)`.
With that done, float64 is not close to being the limiting factor.

**Trim by time, not by block count.** With `blocksize=0` the host chooses
the block size, so a fixed "drop 5 blocks" trim is a different amount of
settling time on every device and rate. The fits drop a fixed 1.0 s from
each end instead.

**Huber weighting plus HAC error bars.** PortAudio timestamps advance in
DMA-buffer quanta, so fit residuals are strongly serially correlated and
the textbook OLS standard error understates the truth — by a factor of
about 4 in these runs. Bartlett-weighted Newey-West repairs it. The fit
also reports the sign imbalance of the down-weighted points, since
glitches that only ever *delay* a callback would bias the slope.

**Find the tone before unwrapping.** The recording deliberately runs
longer than the tone, so it opens and closes with silence, and silent
blocks carry uniformly random phase. Because `np.unwrap` is sequential, a
single random excursion there shifts every later sample by a whole cycle
— a bias, not just noise, which amplitude weighting cannot undo. The
estimator isolates the longest contiguous run of tone first. Before this
fix a 10 s test run showed 2.97 rad rms of phase residual and a 1.8 ppm
error; after it, 0.0004 rad.

**Weight phase blocks by `|z|²`, not `|z|`.** The Fisher information of a
block's phase estimate is proportional to `|z|²`, making it the
maximum-likelihood combination. It also degrades gracefully through a
dropout, where a hard amplitude threshold would either keep a corrupted
block or discard a usable one.

**Choose the tone as an exact submultiple of the rate**, and the
decimation factor as a multiple of `rate/f0`. This puts the demodulated
image at `−2·f0` exactly on a null of the block-averaging filter,
removing the largest systematic term in the phase fit. The decimation is
additionally capped at `rate/(4·f0·ppm_max)` so the per-block phase step
cannot approach π and break the unwrap.

**No `CallbackStop`.** Both callbacks pad with silence past the end of
the buffer and the main thread stops the streams with `abort()`. Raising
`CallbackStop` while the main thread calls `stop()` is the classic way to
wedge `Pa_StopStream` on the CoreAudio backend; not signalling completion
from the callback at all makes the race unreachable rather than merely
unlikely.

**Pre-allocated timestamp logs.** The callbacks run on a CoreAudio
realtime thread, where `list.append` can hit a backing-array realloc and
`queue.Queue.put` takes a lock. The capacity is known from the run
length, so the arrays are allocated once and only written into. Overrun
drops rows and reports it afterwards rather than raising inside the
callback.

**Two independent streams, never a duplex stream.** `sd.Stream` accepts a
device pair, but one full-duplex CoreAudio stream across two physical
interfaces requires an aggregate device, and an aggregate device applies
macOS's own drift correction between its members — which would erase the
very quantity being measured.

### 8.1 Run length

A slope estimate accumulates Fisher information as `T³`, so its standard
error falls as `T^-1.5`: doubling the run buys a factor of 2.8, not 1.4.
Measured directly, at 48 kHz on the test pair:

| `--seconds` | E1/E2 vs E3 agreement |
|---|---|
| 10 | 1.80 ppm |
| 60 | 0.03 ppm |

Below about 30 s the timestamp fits are simply not converged and the
consistency check will fail loudly. **60 s is the default and is the
right choice for almost everything.** Going much beyond 120 s has
diminishing value: the statistical error keeps falling, but crystals
drift thermally at the 1 ppm scale over minutes, so the quantity being
estimated is itself moving.

---

## 9. The scripts

Four files. The first three are this tool, split so that the part which
needs hardware, the part which needs macOS, and the part which is pure
arithmetic are separable. The fourth is the older single-purpose script
they grew out of.

### 9.1 `doc/clock_offset_measure.py` — the tool you run

The capture driver and command-line front end. Everything a user does
normally goes through this file.

- Enumerates devices for `--list-devices`, merging PortAudio's indices
  with CoreAudio's view of each device's real hardware rate list.
- Validates the device pair: rejects a device paired with itself, a
  device without the channels it needs, and virtual or aggregate devices
  whose clock is a host timer rather than an oscillator.
- For each rate: switches both devices' nominal rate, builds the tone,
  opens the two streams, and records the callback timestamps.
- Writes one `capture_<rate>.npz` per rate, then hands each to the
  analysis module and prints the report and summary table.

Contains the realtime-callback code and the `BlockLog` pre-allocated
timestamp log. Imports the other two modules by path, so it runs from any
directory.

### 9.2 `doc/clock_offset_analyze.py` — the estimators

All of the numerics and none of the hardware. Importable as a module, and
runnable standalone against saved captures:

```sh
python3 doc/clock_offset_analyze.py clock_offset_data/capture_*.npz
```

- `fit_rate()` — E1/E2, the Huber-weighted straight-line fit of timestamp
  against frame index, with HAC error bars.
- `fit_tone()` — E3, the tone-region search, complex demodulation and
  `|z|²`-weighted phase-slope fit.
- `choose_tone_frequency()`, `choose_decimation()` — the tone and block
  size for a given rate, per §8.
- `fit_host_clock()`, `apply_host_correction()` — the monotonic-vs-NTP
  regression and the UTC correction.
- `combine()` — the weighted least-squares reconciliation, chi-square and
  Birge scaling.
- `format_report()` — the per-rate report text.

Imports nothing beyond NumPy and the standard library, so a capture taken
on the Mac can be re-analysed anywhere, including on a machine with no
audio hardware at all.

### 9.3 `doc/coreaudio_rate.py` — CoreAudio device and rate control

The macOS-specific layer, talking to CoreAudio through `ctypes`. This is
the file that makes the measurement mean anything (§2): without it the
tool would be measuring resamplers.

- `list_devices()` — every device's real `AudioDeviceID`, name, UID,
  current nominal rate and hardware-supported rate list.
- `get_nominal_rate()` / `set_nominal_rate()` — read and set
  `kAudioDevicePropertyNominalSampleRate`, waiting for the asynchronous
  change to actually take effect rather than trusting the return code.
- `NominalRate` — context manager that sets a rate and restores the
  original afterwards, so an interrupted run does not leave an interface
  parked at 192 kHz.
- `find_by_portaudio_index()` — maps a PortAudio index to its CoreAudio
  device, since PortAudio does not expose the `AudioDeviceID` it uses.

Also runs standalone as a small utility:

```sh
python3 doc/coreaudio_rate.py                 # list devices and their rates
python3 doc/coreaudio_rate.py --set 2 96000   # set PortAudio device 2 to 96 kHz
```

### 9.4 `doc/loopback_clock_cal.py` — the predecessor

The original single-purpose calibration script, kept because it still
does one thing these tools do not: **it measures the analog path delay**
from its marker chirps. It shares this document's device conventions —
PortAudio indices and `--list-devices`, the listing borrowed from
`clock_offset_measure.py` — but keeps its own hard-wired 48 kHz-out /
44.1 kHz-in rate pair and its own estimators. See §10.

---

## 10. `loopback_clock_cal.py`

This is the script that produced the calibration in
`LATENCY_MEASUREMENT_20260725.md` §3. It is narrower than
`clock_offset_measure.py` — one hard-wired rate pair, no rate sweep — but
it measures one thing the newer tool deliberately does not. It now takes
PortAudio device indices and offers `--list-devices`, matching
`clock_offset_measure.py`.

### 10.1 What it does

Plays a 3 kHz tone out of a DAC at 48 kHz and records it on an ADC at
44.1 kHz, with 10 ms marker chirps at 5 s and every 10 s thereafter, then
reports four things:

1. the ADC's rate against the host clock, from `inputBufferAdcTime`;
2. the DAC's rate against the host clock, from `outputBufferDacTime`;
3. the DAC/ADC ratio, both from those two fits and independently from the
   phase slope of the received tone;
4. **the analog path delay** — the time from the DAC timestamp to the ADC
   timestamp, recovered by matched-filtering the marker chirps.

Item 4 is the reason to still reach for it. Because PortAudio's
`outputBufferDacTime` and `inputBufferAdcTime` are both defined at the
converter, what the chirps measure is everything the two devices do
*beyond* what PortAudio accounts for: interpolation filter, USB
transport, cable, anti-alias filter. On the K7 → Rubix24 pair, with both
devices genuinely at their requested rates, that comes to **1.095 ms**
with a standard deviation of 0.002 ms over three chirps.

The earlier 1.316 ms in `LATENCY_MEASUREMENT_20260725.md` §3.1 was
measured with the K7 left at 44.1 kHz nominal while 48 kHz was requested
— that is, with CoreAudio's resampler in the path. The 0.22 ms
difference is the `AudioConverter`'s own latency, which is a useful
independent confirmation that the resampler really was there (§2).

`clock_offset_measure.py` has no equivalent, by choice: chirps raise the
local amplitude of the recorded signal and corrupt the phase blocks they
land in, so removing them is what lets E3 reach a 0.0005 rad residual.
The two scripts trade off against each other rather than one superseding
the other.

### 10.2 Usage

```sh
python3 doc/loopback_clock_cal.py --list-devices
python3 doc/loopback_clock_cal.py --out-device 1 --in-device 2 \
    [--seconds 60] [--base cal] [--analyze-only]
```

| Option | Default | Notes |
|---|---|---|
| `--list-devices` | — | same listing as `clock_offset_measure.py`, then exit |
| `--out-device N` | required | PortAudio index of the device to play from |
| `--in-device N` | required | PortAudio index of the device to record on |
| `--seconds` | `60` | capture length |
| `--base` | `cal` | prefix for the three output files |
| `--analyze-only` | off | re-analyse existing files; needs no device arguments |

Device selection now matches `clock_offset_measure.py`: PortAudio
indices, the same numbering airspy-fmradion's `-P` takes. Before
recording, the script rejects a device paired with itself, an index that
does not exist, and a device without the two channels it needs — and
warns, without stopping, when a device's CoreAudio nominal rate is not
the rate the script is about to ask for, printing the `coreaudio_rate.py`
command that would fix it (§10.3).

Chirps land at 5 s and every 10 s thereafter, so `--seconds` must be at
least 8 for the path delay to have anything to fit.

It writes three files — `<base>_rec.wav`, `<base>_in_ts.npy` and
`<base>_out_ts.npy` — so `--analyze-only` can re-run the analysis later.
Requires `scipy` and `soundfile` in addition to `sounddevice` and NumPy.

The rates are constants in the source, not options:

```python
FS_OUT = 48000
FS_IN = 44100
F_TONE = 3000.0
```

Edit them to change the pair. Running the DAC and ADC at *different*
rates like this is the other thing `clock_offset_measure.py` will not do
— it uses one rate for both devices.

### 10.3 Caveat: its clock figures are subject to §2

`loopback_clock_cal.py` still does not *set* the CoreAudio nominal sample
rate — it only warns. It asks PortAudio for 48 kHz output and 44.1 kHz
input and trusts what it gets, so its DAC and ADC figures are valid only
when the devices already sit at those rates.

This is not hypothetical. The −7.33 ppm it reported for the K7's 48 kHz
DAC was taken with the K7 sitting at 44.1 kHz nominal; the true figure is
about −12.1 ppm (§6). Its Rubix24 result was unaffected, because 44.1 kHz
*was* that device's nominal rate, which is why the −189.54 ppm it gave
still agrees with this tool to under 1 ppm.

So: set the rates first, which the warning will remind you to do:

```sh
python3 doc/coreaudio_rate.py --set 1 48000              # K7 output
python3 doc/coreaudio_rate.py --set 2 44100 --kind input # Rubix24 input
python3 doc/loopback_clock_cal.py --out-device 1 --in-device 2 --seconds 30
```

Run that way, the script agrees with `clock_offset_measure.py` on both
devices — it gave −12.10 ppm for the K7 at 48 kHz and −190.52 ppm for the
Rubix24 at 44.1 kHz, against −12.23 and −190.28 from the sweep, on
completely separate code paths. Its own two internal estimators agreed to
0.245 ppm (+178.453 ppm from the timestamps, +178.208 ppm from the tone).

`coreaudio_rate.py` does not restore the rate afterwards the way
`clock_offset_measure.py` does, so set it back when you are finished.
