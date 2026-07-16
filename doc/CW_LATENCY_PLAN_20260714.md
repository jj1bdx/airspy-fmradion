# CW (Morse/continuous-wave) reception output latency analysis and reduction plan (20260714)

**Date:** 2026-07-14
**Author:** Claude Code (cpp-expert agent)
**Scope:** Where CW-mode (`-m cw`) reception latency comes from in this
codebase, with particular attention to the large-order narrow FIR the
CW path uses, how CW's latency differs from the already-analyzed AM
and FM paths, the measured effect of branch `dev-resampler-lowlatency`
on it, and ranked options to reduce it further. Every number in this
document is either **measured** (executable-level WAV-duration
deficit, a debug-instrumented build's `getInLenBeforeOutStart()`
print, an offline convolution of the exact as-built filter
coefficients, or a decoded-WAV FFT) or **computed** (exact tap-count
arithmetic for a linear-phase FIR's group delay, or a numerically
evaluated IIR transfer function) from the as-built source. Where a
number is a **design study** — an alternative filter that does not
exist in the repository — it is explicitly labeled as such. No
repository file was modified except this one; all builds/signals used
are in the session scratchpad.

Environment: macOS/arm64 (Mac mini 2023), same machine as the FM and
AM documents (`doc/LATENCY_PLAN_20260713.md`,
`doc/AM_LATENCY_PLAN_20260714.md`). Test source: `FileSource` with a
synthetic keyed-carrier IQ file, `srate=1152000`, `raw`,
`format=S16_LE`, default `blklen` (2048, `include/FileSource.h:34`).
Binaries compared: `dev` (`ce34651`, pre-branch,
`scratchpad/airspy-fmradion-dev`) and the current branch
`dev-resampler-lowlatency` at `cde04d0`
(`/Users/kenji/src/airspy-fmradion/build/airspy-fmradion`, built
fresh).

## Executive summary

**CW total steady-state latency, current branch (`cde04d0`),
RTL-SDR-class representative configuration (`srate=1152000`,
`blklen=16384`): ≈151.94 ms**, versus **≈295.84 ms** on `dev`
(`ce34651`) — a measured reduction of **≈143.90 ms**, essentially
identical to the AM path's reduction (`doc/AM_LATENCY_PLAN_20260714.md`),
because CW and AM share the exact same `IfResampler` code path and the
same PortAudio output stage; the only CW-specific term (the narrow CW
FIR, below) is completely unaffected by the branch and so cancels out
of the delta.

**The user's premise is confirmed: CW mode does carry a large-stage
FIR, and after the branch's resampler fix it is now the
second-largest term in CW's latency budget, and the largest one this
project's own code fully controls:**

- **Identity:** `AmDecoder::m_cwfilter`
  (`sfmbase/AmDecode.cpp:36`), a `LowPassFilterFirIQ` built from
  `FilterParameters::jj1bdx_cw_48khz_500hz`
  (`sfmbase/FilterParameters.cpp:682-1708`).
- **Tap count: 2049** (measured by counting the array's numeric
  literals; confirmed symmetric — first coefficient
  `-0.0000031441349395510067` equals the last — i.e. linear-phase).
- **Sample rate: 48 kHz** (not the stale "12kHz" the source comments
  at `sfmbase/AmDecode.cpp:34,38` claim — verified below, §1).
- **Group delay = (2049−1)/2/48000 = 21.3333 ms**, computed exactly
  from the tap count (no estimation needed for a linear-phase FIR).
  This is **8.06x** the AM path's 255-tap `m_amfilter` (2.6458 ms,
  `doc/AM_LATENCY_PLAN_20260714.md` §6) and **larger than the branch's
  entire `IfResampler` term (6.00 ms)** — for the file-source test
  configuration, the CW filter alone now accounts for **73%** of the
  branch's non-output-stage DSP latency (21.33 ms of 29.17 ms; see §8).
- **Design intent** (from `doc/filter-design/48kHz-cw-500Hz-2049taps.json`,
  a pyFDA equiripple design): passband edge 100 Hz, stopband edge
  250 Hz, ≈100 Hz-wide transition band around DC — this is intentional
  and necessary CW selectivity, not a bug or an oversized design;
  §7.3's own from-scratch equiripple search finds essentially the same
  tap count (2084) is needed to hit the same stopband depth at the
  same edges, so the as-built filter is not overdesigned.
- **This delay is largely, but not entirely, an irreducible
  consequence of the filter's own selectivity requirement** (§7): a
  from-scratch equiripple redesign at the same passband/stopband spec
  saves essentially nothing (2049 → 2084 taps, i.e. worse); loosening
  the stopband edge trades away CW selectivity for delay roughly
  linearly (300 Hz edge: 16.08 ms; 500 Hz edge: 8.02 ms); recasting to
  minimum phase cuts the *mean* passband group delay to ≈3.1 ms but
  breaks linear phase (dispersive, envelope-distorting) and requires
  new non-symmetric-FIR code; restructuring as decimate→filter→interpolate
  does **not** reduce group delay at all (confirmed by a swept design
  study — the group delay stays within 21.3–22.5 ms from 48 kHz down
  to 1 kHz intermediate rates), only compute, since group delay for a
  fixed passband/stopband edge in Hz is essentially rate-invariant.
- **Surprise finding:** the CW path's `IfSimpleAgc` (a fast per-sample
  adaptive gain control, rate constant 0.0006 for CW,
  `sfmbase/AmDecode.cpp:71-77`) so strongly reshapes the *observed*
  key-down transient that a naive onset/rise-time measurement on the
  decoded WAV **understates** the FIR's true group delay by more than
  20x in a worst-case (long-silence) test, and by roughly 2x even in a
  realistic 150 ms keying-gap test (§7.5). The exact tap-derived
  21.3333 ms figure — not the onset measurement — is the correct
  number for a latency budget, exactly as the AM doc found for its own
  255-tap filter (`doc/AM_LATENCY_PLAN_20260714.md` §6), but the
  effect is far larger here because CW's `IfSimpleAgc` rate constant
  and its behavior around long silences are more aggressive.

| Term | dev (`ce34651`) | branch (`cde04d0`) | Δ | Label |
|---|---|---|---|---|
| Source batching (test config, `blklen=2048`) | 1.7778 ms | 1.7778 ms | 0 | computed |
| Source batching (RTL-SDR representative, `blklen=16384`) | 14.2222 ms | 14.2222 ms | 0 | computed |
| `IfResampler` (1152k→48k, CW/AM-family path) | 49.5625 ms | 6.0000 ms | **−43.5625 ms** | measured |
| `AmDecoder` `m_cwfilter` FIR (2049 taps @ 48 kHz) | 21.3333 ms | 21.3333 ms | 0 | computed exact |
| `AmDecoder` `m_dcblock` IIR (@ 500 Hz CW tone) | 0.0548 ms | 0.0548 ms | 0 | computed exact |
| PortAudio granted output latency (FiiO K7) | 210.667 ms | 110.333 ms | **−100.334 ms** | measured (cited, FM doc §11) |
| **Total (test config)** | **283.395 ms** | **139.499 ms** | **−143.896 ms** | sum |
| **Total (RTL-SDR representative)** | **295.840 ms** | **151.943 ms** | **−143.897 ms** | sum |

## 1. CW signal path walkthrough

CW is one of the AM-family modes (`ModType::AM`, `DSB`, `USB`, `LSB`,
`CW`, `WSPR`) handled entirely by `AmDecoder`
(`sfmbase/AmDecode.cpp`, `include/AmDecode.h`) — confirmed identical
rate-selection code path to the AM doc's own analysis, verified below:

- `am_target_rate = AmDecoder::internal_rate_pcm` = 48000
  (`main.cpp:685`, `include/AmDecode.h:36`).
- `if_decimation_ratio = ifrate / am_target_rate` for the whole
  `case ModType::AM: ... case ModType::CW: case ModType::WSPR:` arm
  (`main.cpp:730-737`) — **`CW` is in the exact same switch arm as
  `AM`**, not a separate branch; there is no CW-specific rate or
  decimation-ratio logic anywhere in `main.cpp`.
- `demodulator_rate = ifrate / if_decimation_ratio` (`main.cpp:747`),
  algebraically exactly 48000 Hz regardless of `ifrate`. Confirmed in
  this session's own run log for `srate=1152000`: `IF decimation: / 24`,
  `Demodulator rate: 48000 [Hz], audio decimation: / 1` — identical
  numbers to the AM doc's own captured log.
- `IfResampler if_resampler(ifrate, demodulator_rate)`
  (`main.cpp:798-800`) is the same single object used for AM, DSB,
  USB, LSB, CW, and WSPR; `enable_downsampling = (ifrate !=
  demodulator_rate)` (`main.cpp:801`). There is **no CW-specific
  `IfResampler` parameterization or extra resampling stage** — CW
  pays and benefits from exactly the AM path's resampler numbers
  (§3).
- No CW-specific RF/IF tuning-frequency offset exists anywhere in
  `main.cpp`: every `ModType::CW` reference there (lines 528, 583,
  734, 869, 995, 1084, 1134) is a plain switch-case grouping alongside
  AM/DSB/USB/LSB/WSPR (rate selection, log formatting, deemphasis
  display). The only frequency-offset logic specific to CW is
  *downstream* of `IfResampler`, inside `AmDecoder` (next point) — an
  on-air CW station should be tuned so its carrier lands at 0 Hz
  baseband offset (the same "zero-beat" convention the AM doc's test
  signal used), exactly like AM.

`AmDecoder::process()` (`sfmbase/AmDecode.cpp:96-218`), the
`ModType::CW` branch specifically (`AmDecode.cpp:124-129`):

```cpp
case ModType::CW:
  // Apply CW LPF here
  m_cwfilter.process(samples_in, m_buf_filtered1a);
  // Shift up to an audio frequency (500Hz)
  m_cw_finetuner.process(m_buf_filtered1a, m_buf_filtered2);
  break;
```

1. **`m_cwfilter.process()`** — `LowPassFilterFirIQ` built from
   `FilterParameters::jj1bdx_cw_48khz_500hz` (`AmDecode.cpp:36`), a
   **linear-phase lowpass FIR centered at DC**, applied to the complex
   (I/Q) signal, downsample factor 1
   (`include/Filter.h:26-45` for the class, `sfmbase/Filter.cpp:27-34`
   ctor, `Filter.cpp:37-95` `process()` — the implementation exploits
   coefficient symmetry, `Filter.cpp:57,71`: *"NOTE: this assumes the
   filter has symmetric coefficient pairs"*, folding `samples_in[p-k] +
   samples_in[p-(order-k)]` around the half-order pivot, the standard
   2x-multiply-savings trick for a linear-phase FIR). **This is the
   "large-stage FIR" the task asks about** — see the executive summary
   and §7 for its full characterization.
2. **`m_cw_finetuner.process()`** — a table-based frequency shifter
   (`FineTuner`, constructed at `AmDecode.cpp:83`:
   `m_cw_finetuner(internal_rate_pcm / 100, 500 / 100)`, i.e. a
   480-entry table at 48 kHz shifting the spectrum **up by exactly
   500 Hz**). This is a per-sample complex multiply against a
   precomputed table — memoryless, 0 group delay, the same category
   as `FourthConverterIQ` (FM doc §1 row 3). This is *why* an on-tune
   CW carrier (0 Hz baseband) decodes to a 500 Hz audio tone — matching
   the CLI help text `main.cpp:71`: `"- cw (zeroed-in pitch: 500Hz)"`.
   **There is no bandpass filter and no separate carrier-tracking
   PLL** — the design is: filter a lowpass region around DC (where an
   on-tune carrier sits), then shift the whole complex baseband up by
   500 Hz before taking the real part.

Then, continuing through the shared AM-family tail
(`AmDecode.cpp:153-217`):

3. `m_ifagc.process()` — `IfSimpleAgc`, the Tisserand-Berviller
   per-sample adaptive gain control (`sfmbase/IfSimpleAgc.cpp:37-57`),
   constructed with **CW-specific rate 0.0006** (vs. the 0.0003
   default for AM, `AmDecode.cpp:71-77`) and `max_gain = 1000000.0`
   (+120 dB) shared with WSPR. Formally memoryless (0 fixed group
   delay, per the FM/AM docs' classification of AGC stages), but see
   §7.5 for why this stage's *dynamics* dominate CW's observed
   key-down transient far more than for AM.
4. `demodulate_dsb()` — **not** `demodulate_am()`. CW/DSB/USB/LSB/WSPR
   all use `demodulate_dsb` (`AmDecode.cpp:172-178`), which is a
   `volk_32fc_deinterleave_real_32f` — it takes the **real part** of
   the filtered, frequency-shifted complex signal
   (`AmDecode.cpp:229-234`), not an envelope/magnitude detector. This
   is a coherent (product) detector, consistent with CW/SSB being
   phase-coherent modes; it is memoryless and linear, so it does not
   itself introduce distortion of a clean input tone (§7.5's harmonic
   finding traces to `m_ifagc`, not this stage).
5. `m_dcblock.process_inplace()` — `HighPassFilterIir(60 /
   internal_rate_pcm)` (`AmDecode.cpp:45`, `:194`), the same 2nd-order
   IIR DC blocker AM uses, unconditionally applied.
6. `m_afagc.process()` — `AfSimpleAgc`, audio-side adaptive gain/peak
   limiter (`AmDecode.cpp:203`), constructed with **CW/WSPR-specific
   reference 0.24** (vs. 0.6 default for AM) and **rate 0.00125** (vs.
   0.001 default) — `AmDecode.cpp:54-66`. Memoryless in the group-delay
   sense.
7. **`m_deemph` is skipped for CW.** `AmDecode.cpp:211-214`:
   `if (m_mode == ModType::AM) { m_deemph.process_inplace(...); }` —
   CW (like DSB/USB/LSB/WSPR) never runs the de-emphasis IIR at all.
   This differs from what one might assume by analogy with the AM
   doc's budget, and is confirmed directly by reading the guard
   condition.

## 2. Verifying the "12 kHz" comment is stale

`AmDecode.cpp:34,38` carry the comments `// Construct CW narrow filter
(in sample rate 12kHz)` and the equivalent for the SSB filter. These
are leftover from an earlier design. `AmDecoder::internal_rate_pcm =
48000` (`include/AmDecode.h:36`) and, per §1, `IfResampler` already
resamples straight to 48 kHz before `AmDecoder::process()` ever runs
— confirmed by this session's own log (`Demodulator rate: 48000 [Hz]`)
and by git history: `CHANGES.md:56` (20260211-0) documents exactly
this change — *"Changed CW/USB/LSB FIR filters not to down/up-convert
between 48kHz and 12kHz sampling rates, and to use the 2049-tap
filters of 48ksamples/sec ... This significantly reduced output
latency ... The CPU usage increased ~4 times than before"* — i.e. the
project **already made this exact latency-vs-CPU trade once**, in the
direction of *removing* a decimate/filter/interpolate structure to cut
latency. §7.4 below independently confirms, via a from-scratch design
study, that the FIR's own group delay is essentially unaffected by
operating rate — so the historical latency win from that change most
plausibly came from removing the *extra* resampling/interpolation
delay the 48k↔12k round trip added on top of the core filter, not from
the core filter itself running at a different rate. (The old
`doc/filter-design/000-old-values/12kHz-cw-500Hz-383taps.ih_fir` design
is a binary Iowa Hills Kaiser-window file whose exact passband/stopband
edges were not re-extracted for this document — this paragraph is
historical context from `CHANGES.md`, not a re-measurement of the old
path, which no longer exists to test.)

## 3. Measurement methodology

Reused, unmodified, the FM/AM docs' validated methods.

- **Synthetic CW IQ generation** (`scratchpad/gen_cw_onset.py`): 6.0 s,
  S16LE interleaved IQ, `srate=1152000`. Carrier OFF (I=Q=0) for
  `[0, 1.0)` s, then ON at constant baseband amplitude 20000 (I=20000,
  Q=0 — carrier exactly at 0 Hz baseband offset, per §1's "zero-beat"
  convention) for `[1.0, 6.0)` s — one clean key-down edge at exactly
  t=1.0 s.
- **Decode command** (both binaries):
  ```
  airspy-fmradion -t filesource -m cw \
    -c "filename=cw_onset_1152k_s16.raw,srate=1152000,freq=7025000,raw,format=S16_LE" \
    -W cw_out.wav
  ```
  Confirmed in the run log: `Decoding modulation type: cw`, `IF
  decimation: / 24`, `Demodulator rate: 48000 [Hz]`. `FileSource` paces
  blocks to real time (`sfmbase/FileSource.cpp:433-458`), so each 6.0 s
  file took ≈6.0 s of wall-clock time to decode.
- **Output-duration deficit**: `6.000 s − WAV duration` — the
  resampler's steady-state group delay, per the FM doc's
  `DoConsumeLatency` analysis (§1.2 there).
- **Onset / rise-time**: analytic-signal (Hilbert-transform) envelope
  of the decoded mono WAV, 10%/50%/90% threshold crossings relative to
  the settled key-down amplitude (measured from a 3.0–5.0 s window).
- **Debug-instrumented build**: `scratchpad/build-dbg` (already
  configured for this branch with `-DEXTRA_FLAGS="-DDEBUG_IFRESAMPLER
  -DDEBUG_AUDIORESAMPLER"`; rebuilt, bit-identical to source, no code
  changed) and an equivalent debug build of `dev`
  (`scratchpad/dbg-dev`, from the earlier FM investigation, confirmed
  via its printed `Git Commit SHA1: ce34651...`).
- **Offline FIR probe** (`scratchpad/cw_fir_step_probe.py`): the exact
  2049 `jj1bdx_cw_48khz_500hz` coefficients, extracted verbatim from
  `sfmbase/FilterParameters.cpp` (regex-extracted numeric literals,
  count and first/last values checked against the source), convolved
  with a clean unit step at 48 kHz via `scipy.signal.lfilter` — no
  `AmDecoder`, no AGC, no finetuner. This isolates the CW filter's own
  linear-phase step response from the rest of the pipeline.
- **Design study** (`scratchpad/cw_filter_alternatives.py`): alternative
  filter designs (`scipy.signal.remez`, `scipy.signal.minimum_phase`)
  synthesized offline at the same 100 Hz/250 Hz passband/stopband
  edges (from `doc/filter-design/48kHz-cw-500Hz-2049taps.json`) and at
  swept alternative edges/rates — explicitly a design exploration, not
  a measurement of shipped code.
- **Quality check**: Hann-windowed FFT of a settled 2.0–5.0 s segment
  of the decoded WAV; tone peak vs. worst non-tone spectral peak.

## 4. Measured `IfResampler` latency for CW (both branches)

| Configuration | WAV deficit (measured) | Debug build `getInLenBeforeOutStart()` | Per-block accounting (debug build) |
|---|---|---|---|
| `dev` (`CDSPResampler24` preset) | **49.5625 ms** | 57117 in-samples = 49.5807 ms | deficit 2379 out-samples = 49.5625 ms |
| branch (`cde04d0`) | **6.0000 ms** | 6927 in-samples = 6.0130 ms | deficit 288 out-samples = 6.0000 ms |

All three methods agree to within 0.02 ms — the same reconciliation
tolerance the FM/AM docs achieved. The per-block accounting is exact:
for the branch, `total_in=6912000, total_out=287712,
expected_out=288000.0, deficit=288 samples`, and 287712 matches the
actual WAV frame count exactly.

**These numbers are identical to the AM doc's own `IfResampler`
measurement** (`doc/AM_LATENCY_PLAN_20260714.md` §3: 49.5625 ms dev,
6.0000 ms branch) — confirming, independently and empirically (not
just by code inspection), that CW takes the byte-for-byte identical
`IfResampler` code path as AM at these rates. No separate CW-specific
resampler measurement was needed; §1's code-path analysis and this
section's numeric reproduction corroborate each other.

## 5. Effect of `dev-resampler-lowlatency` on CW

| Observable | dev (`ce34651`) | branch (`cde04d0`) | Δ |
|---|---|---|---|
| WAV frames (48 kHz) | 285621 | 287712 | +2091 |
| WAV duration | 5.950437 s | 5.994000 s | +43.563 ms |
| `IfResampler` deficit | 49.5625 ms | 6.0000 ms | **−43.5625 ms** |
| 5%-threshold onset (nominal 1000 ms) | 1000.021 ms | 1000.021 ms | 0 |
| 10%-90% envelope rise time (worst-case test, §7.5) | 0.8542 ms | 0.8542 ms | 0 |
| FFT tone peak | 500.00 Hz | 500.00 Hz | 0 |
| FFT worst spur | −46.37 dBc (1500 Hz) | −46.37 dBc (1500 Hz) | 0 |

The onset, rise time, and spectrum are bit-identical between the two
binaries — exactly as the FM/AM docs found for their own resampler
change: the branch alters only real-time processing delay
(`IfResampler`'s steady-state deficit), never the decoded content or
timeline alignment. **The entire CW-specific benefit of this branch
flows through the identical `IfResampler` mechanism AM uses** (§4);
`AudioResampler` (the other half of the FM doc's resampler fix) is
irrelevant to CW for the same reason the AM doc gives (§1 there):
`AudioResampler` is a private member of `FmDecoder` only, never
constructed or called by `AmDecoder`.

**The CW-specific large FIR (`m_cwfilter`, §7) is completely
unaffected by this branch** — it is untouched code, unchanged on both
sides of the diff, and its 21.3333 ms group delay is identical on
`dev` and on the branch. This has an important consequence: because
the branch shrank `IfResampler` by 43.56 ms, the CW filter — previously
a comparatively small term next to a 49.56 ms resampler — is now
**larger than the resampler that replaced it** (21.33 ms vs. 6.00 ms),
making it the single largest DSP-side term this project's own source
fully controls (the output stage, §9, is larger still but is shared,
platform/driver-level code, not CW-specific).

## 6. Decode-quality verification

FFT of the settled 2.0–5.0 s segment (Hann window, 144000-sample FFT),
identical for both `dev` and branch:

| Metric | Value |
|---|---|
| Tone frequency | **500.00 Hz** (exactly the "-m cw (zeroed-in pitch: 500Hz)" help text promises) |
| Tone level | −15.41 dBFS peak (peak amplitude 0.1697; settled RMS 0.1200 = −18.42 dBFS RMS) |
| Worst spur | 1500.00 Hz (3rd harmonic of the 500 Hz tone) at **−46.37 dBc** |

The decode is clean: a single dominant tone at the pitch the CLI
documents, 46 dB above its worst spur. The 3rd-harmonic spur is not a
resampler artifact (identical on both binaries); §7.5 attributes it to
`IfSimpleAgc`'s per-sample gain adaptation reacting to the amplitude
envelope of the coherently-detected tone, the same class of mechanism
the AM doc found for its own (2nd-harmonic) spur
(`doc/AM_LATENCY_PLAN_20260714.md` §5) — plausible here too since
`demodulate_dsb` itself is a linear operation and cannot generate
harmonics of a clean input.

## 7. The large CW FIR: identity, delay, and design alternatives

### 7.1 Identity and purpose

`AmDecoder::m_cwfilter` (`sfmbase/AmDecode.cpp:36`,
`FilterParameters::jj1bdx_cw_48khz_500hz`,
`sfmbase/FilterParameters.cpp:682-1708`) is the narrowband filter that
gives CW reception its selectivity: a 100 Hz-passband/250 Hz-stopband
lowpass centered on DC, applied to the complex baseband signal before
the 500 Hz pitch-shift (§1). Without it, `demodulate_dsb`'s coherent
detector would pass the *entire* IF bandwidth into the audio path —
this filter is what makes CW reception narrow enough to reject
adjacent signals and most of the receiver noise floor, the entire
reason CW allows much lower minimum discernible signal levels than AM
or SSB. **Its selectivity is the point; its delay is the direct
physical cost of that selectivity** (§7.4 quantifies exactly how
much).

### 7.2 Measured/computed group delay

- **Tap count: 2049**, symmetric (linear-phase Type-I FIR).
- **Group delay = (2049−1)/2 samples = 1024 samples at 48 kHz =
  21.3333 ms**, computed exactly — no estimation needed for a
  linear-phase FIR; this is the delay every steady in-band frequency
  component (including the 500 Hz keyed tone once settled) experiences.
- **Design spec** (`doc/filter-design/48kHz-cw-500Hz-2049taps.json`, a
  pyFDA equiripple/Remez design): passband edge 100 Hz, stopband edge
  250 Hz, target stopband level ≈−80 dB (`A_SB=0.0001`).
- **Verified via offline convolution** of the as-built coefficients
  against a clean step (`scratchpad/cw_fir_step_probe.py`, no AGC/
  finetuner/AmDecoder in the loop): the step response's **50%
  crossing arrives exactly 21.3333 ms after the step** (sample 3424
  vs. step at sample 2400, at 48 kHz) — machine-precision confirmation
  that the tap-count arithmetic and the actual filter's behavior
  agree. The pure-FIR 10%-90% rise time (no AGC) is 2.5417 ms.
- **Frequency response spot-checks** (same probe, `scipy.signal.freqz`):
  |H(100 Hz)| = −0.00 dB (flat passband edge), |H(200 Hz)| = −16.4 dB,
  |H(250 Hz)| = −94.9 dB, |H(300 Hz)| = −120.4 dB — confirming the
  as-built filter meets and somewhat exceeds its documented design
  spec.

### 7.3 Is the as-built filter overdesigned?

No, essentially not. A from-scratch equiripple (Remez) search
(`scratchpad/cw_filter_alternatives.py`, §3 method) for the *shortest*
filter meeting the as-built filter's own measured worst-case stopband
level (−104.35 dB) at the same 100 Hz/250 Hz edges converges to
**2084 taps** — slightly *more* than the as-built 2049, not fewer.
The as-built filter is already close to the Remez-optimal point for
its own spec; there is no "free" latency to recover by simply
re-running the same design more carefully.

### 7.4 Design study: what would reduce the delay, and at what cost

All figures in this subsection are from offline filter designs
synthesized for this document (`scratchpad/cw_filter_alternatives.py`);
none of them exist in the shipped code.

**(a) Loosen the stopband edge (same 100 Hz passband, deeper design
search at each new edge, same ≈−104 dB stopband target):**

| Stopband edge | Transition width | Taps needed | Group delay | Δ vs. as-built (21.33 ms) |
|---|---|---|---|---|
| 200 Hz | 100 Hz | 3223 | 33.56 ms | **+12.23 ms (worse)** |
| 250 Hz (as-built) | 150 Hz | 2049 (2084 in this search) | 21.33 ms (21.70 ms) | 0 |
| 300 Hz | 200 Hz | 1545 | 16.08 ms | **−5.25 ms** |
| 400 Hz | 300 Hz | 982 | 10.22 ms | **−11.11 ms** |
| 500 Hz | 400 Hz | 771 | 8.02 ms | **−13.31 ms** |
| 750 Hz | 650 Hz | 458 | 4.76 ms | **−16.57 ms** |
| 1000 Hz | 900 Hz | 299 | 3.10 ms | **−18.23 ms** |

This is a **direct, roughly linear trade of CW selectivity for
delay**: moving the stopband from 250 Hz to 500 Hz halves the delay
(21.33 → 8.02 ms) but doubles the total passband-to-stopband
bandwidth around DC (from ~500 Hz to ~1000 Hz total, since the filter
is symmetric about 0 Hz) — a real cost in a crowded CW sub-band where
signals are commonly spaced only 100-300 Hz apart at contest-level
density; a 500 Hz-edge filter would let in one or more adjacent
stations that the as-built 250 Hz-edge filter rejects. **This must be
verified by ear/on-air, listening specifically for adjacent-signal
breakthrough, before any such change** — no automated test exists
(`CLAUDE.md`) and this trade-off is fundamentally a selectivity
judgment call, not a numeric optimization.

**(b) Minimum-phase recast** (`scipy.signal.minimum_phase`, same
magnitude response, i.e. same passband/stopband/attenuation, N=1025
taps found by the homomorphic method):

- Passband group delay ranges 2.89–3.71 ms (mean 3.12 ms) — a large
  nominal win over the 21.33 ms linear-phase figure.
- But **not constant across frequency** (a minimum-phase filter is, by
  construction, dispersive) — the passband group delay varies by
  ~0.8 ms across the passband, versus exactly 0 ms variation for the
  as-built linear-phase design. For a keyed CW envelope (a wideband
  event, not a single tone), a dispersive filter distorts the leading
  and trailing edges of dits/dahs asymmetrically (energy front-loaded
  in the impulse response typically produces a fast attack but a
  longer, distorted decay tail or ringing) — this is a genuine risk to
  keying-envelope fidelity and to any downstream automated CW/Morse
  decoder that assumes roughly symmetric make/break timing.
- **Not a drop-in change**: the existing `LowPassFilterFirIQ`
  implementation hard-codes the symmetric-coefficient folding
  optimization (`Filter.cpp:57,71`) for 2x compute savings; a
  minimum-phase (asymmetric) filter needs either a new filter class or
  a generalization of the existing one, at up to 2x the multiply cost
  per tap for the same tap count. Recommended only after on-air/decoder
  compatibility verification — this is the highest-*potential*-benefit,
  highest-*risk* option here.

**(c) Decimate→filter→interpolate — does not reduce group delay.**
A swept design study running the *same* 100 Hz/250 Hz absolute
edges/attenuation target at progressively lower intermediate sample
rates:

| Intermediate rate | Taps at that rate | Group delay |
|---|---|---|
| 48000 Hz (as-built-equivalent) | 2084 | 21.70 ms |
| 24000 Hz | 1046 | 21.77 ms |
| 12000 Hz | 522 | 21.71 ms |
| 6000 Hz | 260 | 21.58 ms |
| 3000 Hz | 134 | 22.17 ms |
| 2000 Hz | 90 | 22.25 ms |
| 1000 Hz | 46 | 22.50 ms |

**Group delay stays within a 21.3–22.5 ms band regardless of the
operating rate** — confirming the intuitive-but-worth-verifying
physics: for a fixed transition bandwidth in Hz and a fixed stopband
attenuation target, a linear-phase FIR's group delay is essentially
invariant to the sample rate it runs at, because the tap count scales
inversely with rate in exactly the way that keeps `N/(2·fs)` constant.
Restructuring the CW filter as a decimate-then-filter-then-interpolate
chain would reduce **compute** (fewer multiply-accumulates per second
at the lower intermediate rate — the historical motivation for the old
12 kHz design, §2) but would **not** reduce the group delay, and would
likely *add* a few milliseconds more from the extra decimation/
interpolation filter stages' own delay (consistent with §2's reading
of `CHANGES.md:56`: removing exactly this kind of structure is what
*reduced* CW's latency in 2026-02, at a measured ~4x CPU cost).

### 7.5 The AGC-masking surprise: onset/rise-time measurements do not show the FIR's group delay

The FM/AM docs both note that onset-threshold detection on a real
step response understates a linear-phase FIR's full group delay,
because a causal filter's impulse response starts responding
immediately (`doc/AM_LATENCY_PLAN_20260714.md` §6). For CW, this
effect is dramatically larger, and tracing it down turned up a second
mechanism: `IfSimpleAgc`'s per-sample adaptive gain
(`sfmbase/IfSimpleAgc.cpp:37-57`) actively reshapes the transient.

**Worst-case test (1.0 s of silence before key-down, §3's test
signal):** the run log shows `IfSimpleAgc`'s gain sitting at
`AGC=+120.0dB` (its `max_gain` ceiling) throughout the silent period —
during silence, the AGC's per-sample update `z = 1 +
distortion_rate*(1 - |x·gain|²)` sees `|x·gain|² ≈ 0`, so `gain *= (1 +
0.0006)` every sample, driving it to the ceiling within a few hundred
milliseconds. The instant the carrier switches on, the AGC massively
overshoots (`|x·gain|²` is now huge), and its multiplicative feedback
slams the gain back down within roughly one status block. **The net
effect on the decoded WAV:**

| Method | 10%-90% rise time | 50%-crossing shift from nominal edge |
|---|---|---|
| End-to-end pipeline (worst-case test) | **0.85 ms** | ≈0 ms |
| End-to-end pipeline (realistic 150 ms keying gap, §3 supplementary test) | **5.88 ms** | **11.90 ms** |
| Pure FIR only (offline probe, no AGC) | **2.54 ms** | **21.33 ms** (exact, by construction) |

A supplementary test (`scratchpad/gen_cw_realistic.py`: key down
0-300 ms to let the AGC settle to its steady ≈+4.3 dB gain, key up for
a 150 ms gap — a plausible "dah" space at ~20 WPM — then key down
again at t=450 ms) shows the transient moving much closer to, but
still short of, the pure-FIR reference, because a 150 ms gap is not
long enough for the AGC to reach its ceiling the way the worst-case
1.0 s silence does. **The practical implication:** the "readable"
onset of a keyed CW element, as it would sound to a listener, is
*faster* than the filter's own 21.33 ms group delay suggests, because
the AGC is (by design) reacting quickly to bring the signal to a
target level — but the underlying, deterministic, tap-count-derived
21.3333 ms is still the correct figure for a latency budget (e.g. if
this decoder's audio were ever synchronized against another timing
reference), since it describes the delay a *sustained* tone
experiences once everything has settled, independent of what happened
immediately before the current block. Both binaries (`dev` and branch)
produce bit-identical numbers in every row of the table above,
confirming this is unrelated to the resampler change.

## 8. Non-resampler CW latency contributors (onset-shift, not deficit)

| Stage | Type | Group delay | How obtained |
|---|---|---|---|
| `m_cwfilter` (`jj1bdx_cw_48khz_500hz`, 2049 taps @ 48 kHz) | linear-phase FIR | **21.3333 ms** | computed exact (§7.2), verified by offline step-response convolution |
| `m_cw_finetuner` (500 Hz table-based shift) | memoryless complex multiply | 0 | source inspection |
| `m_ifagc` (`IfSimpleAgc`, rate 0.0006 for CW) | per-sample adaptive gain | 0 (memoryless); large signal-dependent transient effect, not a fixed delay — see §7.5 | source inspection + §7.5 |
| `demodulate_dsb` (coherent/product detector) | memoryless (`volk_32fc_deinterleave_real_32f`) | 0 | source inspection |
| `m_dcblock` (`HighPassFilterIir`, cutoff 60 Hz) | 2nd-order IIR | **0.0548 ms** at 500 Hz (the CW tone) | computed exact: numerical group-delay evaluation (`-d(phase)/dw`), reusing the AM doc's verified method (`scratchpad/am_iir_groupdelay.py`) at 500 Hz instead of 1 kHz |
| `m_afagc` (`AfSimpleAgc`, rate 0.00125, reference 0.24 for CW) | per-sample adaptive gain | 0 (memoryless) | source inspection |
| `m_deemph` (`LowPassFilterRC`) | **not applied for CW** | 0 (skipped; `AmDecode.cpp:212` gates it to `ModType::AM` only) | source inspection |
| **Total (linear-phase sum, at 500 Hz)** | | **21.3881 ms** | sum |

For reference, at other frequencies within the CW audio range,
`m_dcblock`'s group delay (like any highpass filter, concentrated near
its own 60 Hz corner) is: 3.853 ms at 10 Hz, 3.751 ms at 60 Hz,
1.626 ms at 100 Hz, 0.365 ms at 200 Hz, 0.0136 ms at 1000 Hz — all
negligible next to the CW filter's 21.33 ms at every audio frequency
of interest.

## 9. Output stage

Not remeasured here — identical code and hardware to the FM/AM
measurements, driven by the same `audio_output->write()` call
(`main.cpp:1030`) regardless of `modtype`. Cited directly from
`doc/LATENCY_PLAN_20260713.md` §11:

| Configuration | Requested | Granted `outputLatency` (measured, FM doc §11.2) |
|---|---|---|
| `dev` (`defaultHighOutputLatency`, 40 ms floor) | 40.000 ms | **210.667 ms** |
| branch (`defaultLowOutputLatency`, 25 ms floor) | 25.000 ms | **110.333 ms** |

Device: FiiO K7 USB DAC, macOS/arm64, same machine. See the FM doc §11
for the full decomposition (the gain is entirely from the smaller
request; `paMacCoreChangeDeviceParameters` has no measurable effect on
this device) and its caveat that this is PortAudio's own accounting,
not an acoustic loopback measurement.

## 10. Workarounds (config-level, no code change)

Directly transferable from the FM/AM docs' own §4/§8, since the
mechanisms are source-level and mode-independent:

| Change | Mechanism | Expected saving (CW) | Risk |
|---|---|---|---|
| Reduce RTL-SDR `blklen` from 16384 to 4096 | Shrinks source batching | ~10.67 ms (14.22 → 3.56 ms) | Same as FM doc §4 row 1 |
| Prefer Airspy HF+ at its native 384 kHz over RTL-SDR at 1152 kHz | `IfResampler` still engages (384k→48k rather than 1152k→48k) | Removes RTL's batching term; resampler-side saving not separately measured for this device | Device-dependent |
| None of the SSB fine-tuner/filter stages are extra work specific to CW beyond what §1/§7 already describe | N/A | N/A | N/A |

The output-stage floor (§9) is shared with FM/AM and already reduced
by this branch; further headroom there (110.3 ms → 7.25 ms achievable
if the 25 ms floor itself were dropped, per the FM doc §4/§11.3)
applies identically to CW, pending the same on-air underrun stress
testing the FM doc calls for.

## 11. Architectural changes (code-level), ranked by measured/computed benefit vs. effort

1. **Not recommended without on-air verification, but the single
   largest remaining lever this project's own code controls: loosen
   `jj1bdx_cw_48khz_500hz`'s stopband edge.** Moving from 250 Hz to
   300 Hz saves 5.25 ms (§7.4a) with a comparatively modest
   selectivity cost; further out (500 Hz: −13.31 ms, 750 Hz:
   −16.57 ms) trades progressively more selectivity for delay.
   **Complexity:** low (regenerate the coefficient table with pyFDA or
   `scipy.signal.remez` at a new edge, same 2049-or-fewer-tap
   structure, drop-in replacement for `jj1bdx_cw_48khz_500hz`).
   **Regression risk:** direct trade-off against CW selectivity in
   crowded conditions — must be verified on-air against real adjacent
   CW signals, not just spectrally against this document's clean
   synthetic test signal, before choosing an edge.
2. **High potential, high risk: minimum-phase recast** (§7.4b). Cuts
   mean passband group delay from 21.33 ms to ≈3.1 ms — by far the
   largest single number in this section — but introduces dispersive
   (non-constant) group delay and requires new asymmetric-FIR
   convolution code (the existing `LowPassFilterFirIQ` symmetric-fold
   optimization does not apply). **Complexity:** moderate-high (new
   filter class or generalization, offline filter design and
   coefficient generation, careful listening/decoder-compatibility
   verification of keying-envelope symmetry). **Regression risk:**
   real — envelope asymmetry could affect both human copy and
   automated CW decoders relying on symmetric make/break timing; not
   recommended without dedicated on-air testing specifically targeting
   keying fidelity, not just tone-latency.
3. **Not recommended: decimate→filter→interpolate restructuring**
   (§7.4c). Confirmed by design study to save essentially no group
   delay (results cluster at 21.3–22.5 ms regardless of intermediate
   rate) while adding implementation complexity and, per the project's
   own `CHANGES.md:56` history, likely *re-adding* the extra
   resampling-stage delay that the 2026-02 change specifically removed
   to get CW's latency down in the first place. This would be
   reintroducing the exact structure the project already tried and
   moved away from for this reason.
4. **Everything in the FM/AM docs' own §4/§5 and §8 that is source- or
   output-stage-level** (blklen reduction, further output-latency-floor
   work) transfers unchanged, since those mechanisms are
   mode-independent. The output stage (110.3 ms) remains the single
   largest term in CW's total budget (§9); the CW filter (21.33 ms) is
   the largest term this project fully controls, but is a genuine
   selectivity-vs-latency trade-off (§7.4a), not a "free" fix like the
   resampler parameter change was for FM/AM.
5. **Not recommended: touching `m_dcblock`.** At 0.0548 ms at the CW
   test tone (§8), it is three orders of magnitude below the CW
   filter's contribution and below even the output stage's smallest
   plausible future value; not worth any engineering attention here.

## 12. Summary table (all measured/computed figures)

| # | Term | dev | branch | Δ | Label |
|---|---|---|---|---|---|
| 1 | Source batching (test config, `blklen=2048`) | 1.7778 ms | 1.7778 ms | 0 | computed |
| 1b | Source batching (RTL-SDR representative, `blklen=16384`) | 14.2222 ms | 14.2222 ms | 0 | computed |
| 2 | `IfResampler` (1152k→48k, CW/AM path) | 49.5625 ms | 6.0000 ms | **−43.5625 ms** | measured |
| 3 | `AmDecoder` `m_cwfilter` FIR (2049 taps) | 21.3333 ms | 21.3333 ms | 0 | computed exact |
| 4 | `AmDecoder` `m_dcblock` IIR (@ 500 Hz) | 0.0548 ms | 0.0548 ms | 0 | computed exact |
| 5 | PortAudio granted output latency | 210.667 ms | 110.333 ms | **−100.334 ms** | measured (cited, FM doc §11) |
| | **Total (test config)** | **283.395 ms** | **139.499 ms** | **−143.896 ms** | sum |
| | **Total (RTL-SDR representative)** | **295.840 ms** | **151.943 ms** | **−143.897 ms** | sum |

Not-yet-implemented headroom identified in §7.4:

| Change | Group delay after | Saving vs. branch's current 21.33 ms | Label |
|---|---|---|---|
| Stopband edge → 300 Hz | 16.08 ms | −5.25 ms | design study |
| Stopband edge → 500 Hz | 8.02 ms | −13.31 ms | design study |
| Minimum-phase recast (same spec) | ≈3.1 ms mean | −18.2 ms | design study, dispersive |

## Appendix: artifacts

All in the session scratchpad
(`/private/tmp/claude-501/-Users-kenji-src-airspy-fmradion/a226bec1-4666-4ad2-bdd4-55d31aaddbf4/scratchpad/`),
not in the repository:

- `gen_cw_onset.py`, `cw_onset_1152k_s16.raw` — primary 6.0 s
  synthetic CW test file/generator (§3).
- `gen_cw_realistic.py`, `cw_realistic_1152k_s16.raw` — 2.0 s
  realistic-keying-gap supplementary test (§7.5).
- `cw_out_dev.wav`, `cw_out_branch.wav`, `cw_realistic_out.wav`,
  `cw_realistic_out_dev.wav` — decoded WAV outputs.
- `analyze_cw.py`, `analyze_cw_realistic.py` — WAV-level onset/rise-time/
  FFT analysis scripts.
- `cw_dbgout.wav`, `cw_dbglog.txt` (branch) and `cw_dbgout_dev.wav`,
  `cw_dbglog_dev.txt` (dev) — debug-instrumented
  (`-DDEBUG_IFRESAMPLER`) runs, §4.
- `cw_coeffs.txt` — the 2049 `jj1bdx_cw_48khz_500hz` coefficients,
  extracted verbatim from `sfmbase/FilterParameters.cpp`.
- `cw_fir_step_probe.py` — offline pure-FIR step-response probe, §7.2,
  §7.5.
- `cw_filter_alternatives.py` — design-study script for §7.3/§7.4
  (equiripple re-search, minimum-phase recast, transition-band sweep,
  decimate/filter/interpolate rate sweep).
- `am_iir_groupdelay.py` (from the AM investigation) — reused verbatim
  for `m_dcblock`'s group-delay evaluation at 500 Hz, §8.
- `build-dbg/` — the pre-existing debug CMake build directory for the
  current branch; `dbg-dev` — a pre-existing debug build of `dev`.
