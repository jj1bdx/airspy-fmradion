# Sample = float evaluation (20260716)

This document evaluates the output degradation caused by changing the
audio-path sample type in `include/SoftFM.h` from

```cpp
using Sample = double;
```

to

```cpp
using Sample = float;
```

analyzed with Claude Code (c-expert agent for static analysis) and
verified empirically with an off-the-air piano broadcast recording.
The proposed source changes live uncommitted on branch
`dev-sample-float`.

The proposal was evaluated in two rounds. Round 1 switched everything
`Sample`-typed to float; measurement showed the stereo PLL was the
single dominant contributor to the difference. The final proposal
therefore keeps `PilotPhaseLock`'s recursive state in explicit
`double` (it is not per-sample bulk data, so this costs nothing), and
round 2 measured the result.

## Summary of conclusions (final proposal)

* With `Sample = float` and the `PilotPhaseLock` state kept `double`,
  the decoded output differs from the all-double pipeline by
  **−140 dBFS RMS** (120 dB SNR), which is the quantization floor of
  the float32 comparison files themselves. Mid (L+R) SNR is 118.5 dB;
  side (L−R) SNR is 134.2 dB. The difference RMS is flat at
  ≈ −140 dBFS over the whole 20-second decode — no error
  accumulation.
* The only per-file spectral change is residual ultrasonic
  (≥ 18.5 kHz, pilot-cut stopband) content rising from ≈ −180 dBFS to
  ≈ −152 dBFS. This is the broadband float arithmetic floor of the
  `Sample`-typed 127-tap pilot-cut FIR accumulator and the float
  block boundaries (it is unchanged by the PLL fix, see round 1 vs
  round 2); it is only visible where the signal itself is below it.
  −152 dBFS is ~56 dB below the 16-bit quantization floor and below
  one 24-bit LSB: inaudible by any standard.
* Audible-band (0–18.5 kHz) per-band power is identical between the
  double and float pipelines within ±0.000 dB; total RMS matches to
  0.000 dB.
* Every recursive IIR stage (de-emphasis, DC block, PLL loop filter,
  AGC gain state) keeps hardcoded `double` coefficients/state in
  `Filter.h` independent of the `Sample` typedef, so the switch never
  touches their recursions. `PilotPhaseLock` was the one exception,
  and the final proposal closes it.
* CPU time for the 20-second decode dropped from 1.48 s to 1.24 s of
  user time (≈ 16 % less CPU); keeping the PLL in double costs
  nothing measurable. Output is bit-identical across repeated runs of
  the same binary (deterministic pipeline). `SampleVector` buffers
  halve in memory footprint and memory bandwidth.

Verdict: the final proposal causes no audible or practically
measurable degradation for FM broadcast decoding. Against float32
outputs, the change is at the measurement floor; against 16-bit
outputs and real FM channel noise, it is 40–120 dB below anything
observable.

## Proposed source changes

`git diff --stat` on `dev-sample-float`:

```
 include/PilotPhaseLock.h   | 11 +++++++----
 include/SoftFM.h           |  2 +-
 main.cpp                   |  6 +-----
 sfmbase/AmDecode.cpp       |  5 +++--
 sfmbase/AudioOutput.cpp    |  7 ++++---
 sfmbase/AudioResampler.cpp | 16 +++++++++++-----
 sfmbase/FmDecode.cpp       |  7 ++++---
 sfmbase/NbfmDecode.cpp     |  5 +++--
 sfmbase/PilotPhaseLock.cpp | 22 ++++++++++++----------
 9 files changed, 46 insertions(+), 35 deletions(-)
```

* `include/SoftFM.h`: `using Sample = float;`. `SampleVector` and
  `SampleCoeff` (FIR coefficient tables in `FilterParameters.cpp`)
  track the change automatically.
* `include/PilotPhaseLock.h`, `sfmbase/PilotPhaseLock.cpp`: the PLL
  state (`m_minfreq`, `m_maxfreq`, `m_freq`, `m_phase`,
  `m_pilot_level`, `m_freq_err`) and the per-sample locals feeding
  the recursion (`psin`, `pcos`, `x`, `phasor_i/q`,
  `new_phasor_i/q`, `phase_err`, `new_phase_err`) are typed explicit
  `double` instead of `Sample`. `m_phase`/`m_freq` are live recursive
  accumulators whose float rounding becomes 38 kHz subcarrier phase
  noise directly; only the `samples_out[i]` stores narrow to
  `Sample`. This state is O(1) per PLL, so the double precision is
  free.
* `sfmbase/FmDecode.cpp`, `sfmbase/NbfmDecode.cpp`,
  `sfmbase/AmDecode.cpp`: the `volk_32f_convert_64f` calls that
  copied the float discriminator output (`IQSampleDecodedVector`)
  into the `SampleVector` baseband buffer degenerate into same-type
  copies (VOLK has no 32f→32f "convert" kernel); replaced with
  `std::copy_n`.
* `sfmbase/FmDecode.cpp` (`demod_stereo`): `volk_64f_x2_multiply_64f`
  → `volk_32f_x2_multiply_32f`.
* `main.cpp`: the audio-level measurement no longer needs the
  double→float conversion buffer `audiosamples_float`;
  `Utility::samples_mean_rms` is called directly on the (now float)
  `audiosamples`.
* `sfmbase/AudioOutput.cpp`:
  * `SndfileOutput::write`: `sf_write_double` → `sf_write_float`
    (libsndfile performs the format scaling internally for all output
    modes: S16/FLOAT raw/WAV and MP3).
  * `PortAudioOutput::write`: the double→float conversion into
    `m_floatbuf` becomes a same-type `std::copy_n`. (Follow-up
    cleanup possible: pass `samples.data()` to `Pa_WriteStream`
    directly and drop `m_floatbuf`.)
* `sfmbase/AudioResampler.cpp`: r8brain's `CDSPResampler::process`
  is double-only, so the former direct `const_cast<double *>` pass
  no longer compiles. The input is now widened into a `DoubleVector`
  and the output narrowed back to `Sample`, mirroring the pattern
  `IfResampler.cpp` already uses on the (float) IQ path. r8brain's
  internal filter math stays entirely in double either way.
* Explicit `#include <algorithm>` added where `std::copy_n` is used.

## Static analysis (c-expert)

Key findings (from the round-1 analysis; the `PilotPhaseLock` item
motivated the final proposal):

* **Double-protected recursions (unaffected)**: `FirstOrderIirFilter`
  and `BiquadIirFilter` in `Filter.h` hardcode `double` for all
  coefficients and state, independent of `Sample`. This covers the
  50 µs de-emphasis (`LowPassFilterRC`, pole at −0.94926), the 4.8 Hz
  DC-blocking high-pass (`HighPassFilterIir`, a near-unity-pole
  design that would be genuinely dangerous in float), the pilot PLL
  loop filter (whose `b0`/`b1` cancel to 5–6 significant digits),
  and the AF/IF AGC gain states. The `Sample` switch only rounds
  their per-sample I/O boundaries (single-ULP, non-recursive,
  ≈ −118 dBFS each).
* **The one real precision change**: `PilotPhaseLock::m_phase` and
  `m_freq` were `Sample`-typed live recursive state. At the pilot's
  phase increment (≈ 0.3108 rad/sample) one float ULP is
  ≈ 3.7×10⁻⁸ rad, giving an estimated 38 kHz subcarrier phase-noise
  floor of 10⁻⁷–10⁻⁶ rad, plus a theoretical frequency-tracking dead
  zone under unrealistically clean signal conditions. Round-1
  measurement confirmed this as the dominant difference source; the
  final proposal removes it by pinning the PLL state to `double`.
* **FIR summation**: the 127-tap pilot-cut filter accumulates in
  `Sample`; uncorrelated-rounding estimate √127 × 2⁻²³ ≈ −117 dBFS
  RMS (worst-case coherent bound −96 dBFS, not reached in practice).
  This is the source of the remaining ultrasonic stopband residue
  measured below.
* **Net static estimate**: added audio-path noise floor ≈ −110 to
  −120 dBFS — 20–50 dB below 16-bit output quantization and 50–95 dB
  below realistic FM broadcast SNR.

## Empirical evaluation

### Method

Input: `wrk/piano_iqtest.wav`, an off-the-air piano broadcast on
82.5 MHz recorded with Airspy HF+
(`airspyhf_rx -f 82.5 -z -a 384000 -n 7680000 -w`; 20 seconds of
384 kHz zero-offset IQ, float32 WAV).

Each build decoded the same file to float32 WAV, so the
file-quantization floor is identical and any difference is
internal-precision-induced:

```
airspy-fmradion -t filesource -m fm \
  -c srate=384000,freq=82500000,zero_offset,filename=wrk/piano_iqtest.wav \
  -G out.wav
```

All builds locked stereo (pilot level ≈ 0.105). Outputs are 959,899
stereo frames at 48 kHz. Statistics below skip the first second
(PLL/AGC/resampler settling) and were computed in float64 (numpy).
Repeated runs of each binary produce byte-identical output (same
MD5), so the comparisons are deterministic.

### Round 2 (final proposal): float pipeline + double PLL state

Difference against the all-double baseline:

| metric | value |
|---|---|
| reference RMS | −20.14 dBFS |
| difference RMS | 9.77×10⁻⁸ (−140.2 dBFS) |
| max abs difference | 8.64×10⁻⁷ (−121.3 dBFS) |
| overall difference SNR | 120.1 dB |
| mid (L+R)/2 difference RMS | −140.3 dBFS (SNR 118.5 dB) |
| side (L−R)/2 difference RMS | −159.5 dBFS (SNR 134.2 dB) |
| difference RMS vs time | flat, −140.0 to −140.4 dBFS every second |

A −140 dBFS difference between two float32 files carrying a
−20 dBFS signal is the rounding of the file format itself: the final
proposal is transparent at the resolution the output format can
express. The per-second profile shows no error growth over the
20-second decode.

Per-file band power (relative to each file's total power, mono sum):

| band | double | float | delta |
|---|---|---|---|
| 0–5 kHz | −0.00 dB | −0.00 dB | ±0.000 |
| 5–10 kHz | −55.08 dB | −55.08 dB | ±0.000 |
| 10–15 kHz | −67.96 dB | −67.96 dB | ±0.000 |
| 15–18.5 kHz | −70.34 dB | −70.34 dB | ±0.000 |
| 18.5–19.5 kHz | −159.73 dB | −132.44 dB | +27.3 |
| 19.5–23 kHz | −155.95 dB | −127.09 dB | +28.9 |
| 23–24 kHz | −161.20 dB | −132.61 dB | +28.6 |

Total RMS: −20.142 dBFS in both. The ≥ 18.5 kHz rows are the
pilot-cut stopband: the double pipeline reaches an (irrelevantly)
deep ≈ −180 dBFS residue there, while float arithmetic floors at
≈ −152 dBFS. These rows are identical between round 1 and round 2,
proving the residue comes from the `Sample`-typed FIR accumulation
and float block boundaries, not from the PLL. −152 dBFS is below one
24-bit LSB.

### Round 1 (rejected variant): fully float, PLL state included

Same measurement with `PilotPhaseLock`'s state still `Sample`-typed
(float):

| metric | value |
|---|---|
| difference RMS | 7.65×10⁻⁵ (−82.3 dBFS) |
| max abs difference | 8.96×10⁻⁴ (−61.0 dBFS) |
| mid (L+R)/2 difference RMS | −140.3 dBFS (SNR 118.5 dB) |
| side (L−R)/2 difference RMS | −82.3 dBFS (SNR 57.0 dB) |

The mono path was already transparent; the entire difference budget
was in the stereo (L−R) channel, rising with frequency at roughly
+20 dB/decade — the signature of a small 38 kHz demodulation
phase/timing difference. Per-band signal *power* was still identical
within 0.03 dB across the audible band (the float PLL walks a
slightly different, equally valid phase trajectory around the same
noisy broadcast pilot — decorrelation, not added noise), so even
this variant was almost certainly inaudible. But since pinning one
PLL's worth of scalar state to `double` removes the effect entirely
at zero cost, the final proposal does so.

### Reference floors

| floor | level |
|---|---|
| final-proposal difference RMS | −140 dBFS (measurement floor) |
| 16-bit output quantization noise (undithered) | −101 dBFS |
| float32 output file quantization (at −20 dBFS signal) | ≈ −140 dBFS |
| FM broadcast channel SNR (best case) | 50–70 dB |

### Performance

For the 20-second file (wall time is real-time-paced by the file
source):

| build | user CPU |
|---|---|
| double (baseline) | 1.48 s |
| float, PLL state float (round 1) | 1.27 s |
| float, PLL state double (final) | 1.24 s |

≈ 16 % less CPU than the double baseline; the double PLL state costs
nothing measurable.

## Caveats

* This branch (`dev-sample-float`) is for experiments; the changes
  are intentionally left uncommitted.
* The remaining (inaudible, ≈ −152 dBFS) ultrasonic stopband residue
  comes from the `Sample`-typed accumulator in
  `LowPassFilterFirAudio` (`Filter.cpp`). Eliminating it would
  require a `double` accumulator in that FIR loop; given it sits
  below one 24-bit LSB, this is not proposed.
* AM/NBFM paths were converted and compile cleanly but were not
  empirically measured; their recursive filters are all
  double-protected, and the c-expert analysis found no additional
  risk sites there.
