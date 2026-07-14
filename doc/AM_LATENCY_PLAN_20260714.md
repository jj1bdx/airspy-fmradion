# AM broadcast reception output latency analysis and reduction plan (20260714)

**Date:** 2026-07-14
**Author:** Claude Code (cpp-expert agent)
**Scope:** Where AM/DSB/USB/LSB/CW/WSPR ("AM-family") reception latency
comes from in this codebase, how it differs from the FM path already
analyzed in `doc/LATENCY_PLAN_20260713.md`, the measured effect of
branch `dev-resampler-lowlatency` on it, and ranked options to reduce
it further. Every number in this document is either **measured**
(executable-level WAV-duration deficit, a debug-instrumented build, or
a standalone r8brain probe compiled exactly as the shipped binary
compiles it — see `doc/LATENCY_PLAN_20260713.md` §9.4 for why that
matters) or **computed** from the as-built source (exact filter
coefficients, evaluated numerically — not a rule-of-thumb estimate).
Anything not directly measured is explicitly labeled "computed" and
inherits the FM doc's own caveats. No repository file was modified
except this one; all builds/signals used are in the session scratchpad.

Environment: macOS/arm64 (Mac mini 2023), same machine as the FM
document. Test source: `FileSource` with a synthetic AM IQ file,
`srate=1152000`, `raw`, `format=S16_LE`, default `blklen` (2048, see
§2). Binaries compared: `dev` (`ce34651`, pre-branch) and the current
branch `dev-resampler-lowlatency` at `cde04d0` (built fresh from
`/Users/kenji/src/airspy-fmradion`).

## Executive summary

**AM total steady-state latency, current branch (`cde04d0`), RTL-SDR-class
representative configuration (`srate=1152000`, `blklen=16384`): ≈133.3 ms**,
of which the **PortAudio output stage (110.3 ms, shared with FM, cited
from `doc/LATENCY_PLAN_20260713.md` §11) is now the single dominant
term**, followed by this branch's own `IfResampler` (6.00 ms,
**measured**). Before this branch (`dev`, `ce34651`), the same
configuration measured **≈277.2 ms**. The branch's net measured
reduction is **≈143.9 ms** (43.6 ms of it is AM-specific DSP, the
remaining 100.3 ms is the shared macOS output-stage change already
documented for FM).

Two findings are specific to AM and worth stating up front:

1. **AM has only one resampler stage**, not two. `AmDecoder` runs
   entirely at `internal_rate_pcm = 48000` Hz
   (`include/AmDecode.h:36`) and never touches `AudioResampler` — that
   class is instantiated only inside `FmDecoder`
   (`sfmbase/FmDecode.cpp`, per the FM doc §8.1). For AM,
   `main.cpp` sets `IfResampler`'s output rate directly to 48000 Hz
   (`main.cpp:747`, `demodulator_rate == am_target_rate` always), so
   **100% of this branch's DSP-latency benefit for AM flows through
   `IfResampler` alone** — unlike FM, where the benefit is split
   between `IfResampler` and `AudioResampler`.
2. **The branch's `IfResampler` parameters were tuned for FM's shape
   of the problem, and AM pays a hidden cost for it.**
   `IfResampler`'s `req_trans_band`/`req_atten` (`include/IfResampler.h:42-43`)
   are fixed constants — `10.0` / `140.0` — shared by every mode.
   r8brain's `ReqTransBand` is a **percentage of the output Nyquist
   frequency** (`r8brain-free-src/CDSPResampler.h:87-90`). For FM,
   `IfResampler`'s output is 384 kHz (`FmDecoder::sample_rate_if`),
   so 10% is an absolute transition band of 19.2 kHz — measured 0.75 ms
   as-built (FM doc §9.3). For AM, the very same code path outputs
   **48 kHz**, so the identical 10% is only a 2.4 kHz absolute
   transition band — **8x narrower in Hz** — and measures **6.00 ms**,
   8x FM's figure, for the identical source-code parameters. AM's
   actual audio bandwidth is only 4.5 kHz
   (`AmDecoder::bandwidth_pcm`, `include/AmDecode.h:38`), a fraction of
   FM's 15 kHz + 19 kHz pilot constraint, so there is room to give AM
   its own, much looser parameters; §9 measures how much (down to
   0.74 ms, a further ~5.3 ms).

| Term | dev (`ce34651`) | branch (`cde04d0`) | Measured how |
|---|---|---|---|
| Source batching (test config, `blklen=2048`) | 1.778 ms | 1.778 ms | computed, unchanged |
| Source batching (RTL-SDR representative, `blklen=16384`) | 14.222 ms | 14.222 ms | computed, unchanged |
| `FourthConverterIQ` | 0 (skipped in this test) | 0 | see §1 |
| `IfResampler` (1152k→48k, AM) | **49.5625 ms** | **6.0000 ms** | measured (§3, §4) |
| `AmDecoder` amfilter FIR (255 taps @ 48k) | 2.646 ms | 2.646 ms | computed exact, unchanged |
| `AmDecoder` DC-block + de-emphasis IIR (@ 1 kHz) | 0.076 ms | 0.076 ms | computed exact, unchanged |
| PortAudio granted output latency | 210.667 ms | 110.333 ms | measured, cited from FM doc §11 |
| **Total (RTL-SDR representative)** | **277.17 ms** | **133.28 ms** | sum |
| **Total (this test's config)** | **264.73 ms** | **120.83 ms** | sum |

## 1. AM signal path walkthrough

`main.cpp`'s AM-family rate selection (`ModType::AM`, `DSB`, `USB`,
`LSB`, `CW`, `WSPR` share one path):

- `am_target_rate = AmDecoder::internal_rate_pcm` = 48000
  (`main.cpp:685`, `include/AmDecode.h:36`).
- `if_decimation_ratio = ifrate / am_target_rate` (`main.cpp:736`, the
  `case ModType::AM: ... case ModType::WSPR:` arm of the switch at
  `main.cpp:730-737`).
- `demodulator_rate = ifrate / if_decimation_ratio` (`main.cpp:747`)
  — algebraically exactly `am_target_rate` = 48000 Hz, **regardless of
  the source's `ifrate`**. For `srate=1152000` this is confirmed in
  the program's own log: `Demodulator rate: 48000 [Hz], audio
  decimation: / 1` (captured in this session's run logs).
- `enable_downsampling = (ifrate != demodulator_rate)` (`main.cpp:801`)
  — true whenever `ifrate != 48000`, so `IfResampler` engages directly
  from the source rate to 48 kHz in one stage. There is no second,
  audio-rate resampler for AM: `AudioResampler` is a member only of
  `FmDecoder` (per the FM doc §8.1), and `main.cpp` constructs `FmDecoder
  fm(...)` and `NbfmDecoder nbfm(...)` unconditionally
  (`main.cpp:841-853`) even when `modtype == ModType::AM` — but their
  `.process()` is never invoked in that case (`main.cpp:980-1001`), so
  those objects (and `AudioResampler` inside `fm`) sit idle. This was
  confirmed directly: a debug build with `-DDEBUG_AUDIORESAMPLER`
  still prints `AudioResampler latency = 809` at startup (construction
  time) during an AM-mode run, but the accompanying per-block
  `AudioResampler` process trace never appears — only `IfResampler`'s
  does (§2).
- `FourthConverterIQ fourth_downconverter` (`main.cpp:796`) is
  engaged when `enable_fs_fourth_downconverter = !up_srcsdr->is_low_if()`
  is true (`main.cpp:680`, applied at `main.cpp:935-942`). This is a
  **source-level** property, independent of `modtype`:
  `RtlSdrSource::is_low_if()` returns `false`
  (`sfmbase/RtlSdrSource.cpp:282`) so RTL-SDR engages it;
  `FileSource::is_low_if()` returns `!m_zero_offset`
  (`sfmbase/FileSource.cpp:291`), and `m_zero_offset` defaults to
  `false` (`sfmbase/FileSource.cpp:38`) unless the config string
  contains `zero_offset`, so **the default `FileSource` test used here
  skips it** — exactly the same situation the FM doc's own file-source
  test was in (its command line likewise omitted `zero_offset`). This
  choice was deliberate: it reuses the FM doc's validated test
  methodology and keeps `FourthConverterIQ`'s well-established "no
  memory, ~0 ms" contribution (FM doc §1 row 3) out of the
  measurement, which is fine since it is architecturally identical
  for AM and FM (same class, same per-sample complex multiply, no
  state).
- `IfResampler if_resampler(ifrate, demodulator_rate)` is constructed
  once (`main.cpp:798-800`) and driven every block at
  `main.cpp:945-946` (`if_resampler.process(if_shifted_samples,
  if_samples)`), producing `if_samples` at 48 kHz.
- `AmDecoder am(amfilter_coeff, modtype)` is constructed at
  `main.cpp:836-838` with `amfilter_coeff` selected by `-f` filter type
  (`main.cpp:808-833`; default type gives
  `FilterParameters::jj1bdx_am_48khz_default`,
  `sfmbase/FilterParameters.cpp:272`, 255 taps — see §6). It is driven
  at `main.cpp:998` (`am.process(if_samples, audiosamples)`), inside
  the `case ModType::AM: ... case ModType::WSPR:` arm
  (`main.cpp:991-1000`).
- `audio_output->write(std::move(audiosamples))` (`main.cpp:1030`) —
  the same single output call FM uses, at the same `pcmrate = 48000`
  (`main.cpp:283`), `stereo = false` for every AM-family mode
  (`main.cpp:514-532`), feeding the same `PortAudioOutput` class as FM
  (§7 cites the FM doc's measurement of that stage directly).

`AmDecoder::process()` (`sfmbase/AmDecode.cpp:96-218`) stage order for
`ModType::AM` specifically (the `DSB`/`USB`/`LSB`/`CW`/`WSPR` branches
add fine-tuner/CW/SSB filter stages not exercised by an AM broadcast
signal and are out of scope here):

1. `m_amfilter.process(samples_in, m_buf_filtered2)` —
   `LowPassFilterFirIQ` with `jj1bdx_am_48khz_default` (255 taps),
   downsample factor 1 (`sfmbase/AmDecode.cpp:32`, `:101`).
2. `m_ifagc.process(m_buf_filtered2, m_buf_filtered3)` —
   `IfSimpleAgc`, a per-sample adaptive (Tisserand-Berviller) gain
   control, memoryless in the group-delay sense
   (`sfmbase/AmDecode.cpp:157`, `sfmbase/IfSimpleAgc.cpp:34-53`).
3. `demodulate_am(m_buf_filtered3, m_buf_decoded)` — envelope detector,
   `volk_32fc_magnitude_32f` (`sfmbase/AmDecode.cpp:170`, `:221-226`),
   memoryless.
4. `m_dcblock.process_inplace(m_buf_baseband_demod)` —
   `HighPassFilterIir(60 / internal_rate_pcm)`, a 2nd-order IIR DC
   blocker (`sfmbase/AmDecode.cpp:45`, `:194`).
5. `m_afagc.process(m_buf_baseband_demod, m_buf_baseband)` —
   `AfSimpleAgc`, audio-side adaptive gain/peak limiter, memoryless
   (`sfmbase/AmDecode.cpp:203`, `sfmbase/AfSimpleAgc.cpp:33-52`).
6. `m_deemph.process_inplace(m_buf_baseband)` — `LowPassFilterRC`
   (`sfmbase/AmDecode.cpp:49`, `:212-214`), applied **only** when
   `m_mode == ModType::AM` (not for DSB/USB/LSB/CW/WSPR).

## 2. Measurement methodology

Reused, unmodified, the FM doc's validated methods (§9, §9.4, §3 of
that document):

- **Synthetic AM IQ generation**
  (`scratchpad/gen_am_onset.py`): 6.0 s, S16LE interleaved IQ,
  `srate=1152000`, baseband (0 Hz) complex signal matching the
  `FourthConverterIQ`-skipped configuration of §1. Because the
  simulated receiver's carrier sits exactly at DC in this
  configuration, the complex baseband signal is purely real: `I(t) =
  20000 * (1 + 0.5*sin(2*pi*1000*t))` for `t >= 1.0 s` (else constant
  20000, an unmodulated carrier), `Q(t) = 0`. Peak amplitude 30000,
  well inside int16 range. `AmDecoder::demodulate_am()`'s
  `sqrt(I^2+Q^2)` reduces to `|I| = I` exactly since `I` never goes
  negative (0.5x–1.5x carrier), so this is a clean envelope-detector
  test signal by construction.
- **Decode command** (both binaries):
  ```
  airspy-fmradion -t filesource -m am \
    -c "filename=am_onset_1152k_s16.raw,srate=1152000,freq=666000,raw,format=S16_LE" \
    -W am_out.wav
  ```
  `FileSource` paces blocks to real time
  (`sfmbase/FileSource.cpp:433-458` — unchanged from the FM doc), so
  each 6.0 s file took ≈6.0–6.4 s of wall-clock time to decode
  (confirmed via `time`).
- **Output-duration deficit**: `6.000 s − WAV duration`. As in the FM
  doc, r8brain consumes its group delay from the output timeline
  (`DoConsumeLatency`, FM doc §1.2), so this deficit is exactly the
  resampler chain's steady-state real-time group delay; downstream
  FIR/IIR stages emit 1:1 and contribute nothing to it.
- **Tone onset**: first sample (scanning from 0.3 s, after the
  DC-blocker's own startup transient settles — see §6) whose magnitude
  exceeds a small threshold.
- **Debug-instrumented build**: `cmake -S
  /Users/kenji/src/airspy-fmradion -B scratchpad/build-dbg
  -DEXTRA_FLAGS="-DDEBUG_AUDIORESAMPLER -DDEBUG_IFRESAMPLER"` (the
  scratchpad build directory already existed, configured for the
  current branch; rebuilt with `cmake --build scratchpad/build-dbg
  --target airspy-fmradion -j 8` — bit-identical to source, no code
  changed). `AmDecode.cpp` has **no** `DEBUG_AMDECODE`-style hooks
  (checked: `grep -rn "DEBUG_" sfmbase/AmDecode.cpp sfmbase/IfSimpleAgc.cpp
  sfmbase/AfSimpleAgc.cpp` finds nothing), so the debug build's only
  useful print for AM is `IfResampler`'s.
- **Standalone r8brain probe**
  (`scratchpad/r8b_am_deficit.cpp`, modeled on
  `scratchpad/r8b_deficit.cpp` from the FM investigation): compiled
  **with no `R8B_*` defines** —
  `c++ -std=c++20 -O2 -I/Users/kenji/src/airspy-fmradion/r8brain-free-src
  r8b_am_deficit.cpp -o r8b_am_deficit` — matching exactly how
  `sfmbase` compiles the header-only r8brain templates (FM doc §9.4:
  the `R8B_EXTFFT`/`R8B_FASTTIMING`/`R8B_PFFFT_DOUBLE` defines never
  reach `sfmbase`, only `main.cpp`, and even there they configure a
  `libr8b.a` that is never linked into anything `sfmbase` calls). The
  probe feeds a 6.0 s stream in 2048-sample chunks (matching
  `FileSource::default_block_length`, `include/FileSource.h:34`) through
  `r8b::CDSPResampler` with the exact constructor arguments
  `IfResampler` uses, and reports both the steady-state output-count
  deficit and `getInLenBeforeOutStart()`.
- **Quality check**: FFT (Hann-windowed) of a settled 3.0 s segment
  (2.0–5.0 s) of the decoded WAV, tone peak vs. worst non-tone spectral
  peak.

## 3. Measured `IfResampler` latency for AM (both branches)

| Configuration | Params | WAV deficit (measured) | Probe deficit (measured, no-defines) | `getInLenBeforeOutStart()` (measured) |
|---|---|---|---|---|
| `dev` (`CDSPResampler24` preset) | tb=2.0%, att=180.15 dB | **49.5625 ms** | 49.5625 ms | 57117 in-samples = 49.5807 ms |
| branch (`cde04d0`) | tb=10.0%, att=140.0 dB | **6.0000 ms** | 6.0000 ms | 6927 in-samples = 6.0130 ms |

The WAV-deficit method, the debug-instrumented executable
(`getInLenBeforeOutStart()` printed at construction: `IfResampler latency
= 6927` for the branch), and the standalone no-defines probe all agree
to within 0.02 ms — the same tolerance the FM doc's cross-validation
achieved (§9.3 of that document). The debug build's per-block
accounting also reconciles exactly:

```
blocks: 3375  total_in: 6912000  total_out: 287712
expected_out (total_in * 48000/1152000): 288000.0
deficit_samples: 288.0  deficit_ms: 6.0
```

matching the WAV frame count (287712) and duration (5.994000 s) of the
branch's `am_out_branch.wav` exactly.

Sanity cross-check against the FM doc's own IF-resampler numbers for
the **same code path, same rates, same parameters**, but with FM's
384 kHz output instead of AM's 48 kHz output:

| Configuration | Output rate | Group delay (measured, both docs) |
|---|---|---|
| `IfResampler`, dev preset, → 384 kHz (FM) | 384000 | 6.19 ms (FM doc §9.3) |
| `IfResampler`, dev preset, → 48 kHz (AM) | 48000 | **49.56 ms** (this doc) |
| `IfResampler`, branch params, → 384 kHz (FM) | 384000 | 0.75 ms (FM doc §9.3) |
| `IfResampler`, branch params, → 48 kHz (AM) | 48000 | **6.00 ms** (this doc) |

Both rows show almost exactly an 8x increase for AM relative to FM
(49.56/6.19 = 8.01, 6.00/0.75 = 8.00) — exactly the ratio of output
Nyquist frequencies (192 kHz / 24 kHz = 8), confirming the mechanism
described in the executive summary: `ReqTransBand`'s percentage basis
is anchored to output Nyquist, and AM's output Nyquist is 8x smaller.

### 3.1 Headroom: mode-aware parameters (measured, not yet implemented)

AM's actual audio bandwidth is 4.5 kHz (`AmDecoder::bandwidth_pcm`,
`include/AmDecode.h:38`) against a 24 kHz output Nyquist — a much
larger safety margin than FM has (15 kHz audio + 19 kHz pilot against
the same 24 kHz). The probe was extended to test looser parameters at
AM's exact rates (1152k→48k):

| Params | Deficit (measured) | Passband edge (computed: `24000*(1-tb/100)`) |
|---|---|---|
| tb=10.0%, att=140 dB (branch, current) | 6.0000 ms | 21.6 kHz |
| tb=15.0%, att=120 dB (FM's own audio-resampler fix, FM doc §7.4) | 2.9375 ms | 20.4 kHz |
| tb=37.0%, att=120 dB | 1.5417 ms | 15.12 kHz |
| tb=40.0%, att=120 dB | 1.5625 ms | 14.4 kHz |
| tb=44.0%, att=100 dB (near r8brain's 45% cap) | **0.7292 ms** | 13.44 kHz |

r8brain caps `ReqTransBand` at 45%
(`r8brain-free-src/CDSPFIRFilter.h:87-90`,
`getLPMaxTransBand()`), so 44% is close to the practical floor for
this ratio. Even the most conservative of these (tb=15%, matching
what FM's own audio path already uses at the same 48 kHz output rate)
would save a further **3.06 ms** over the branch's current AM figure,
with a passband edge (20.4 kHz) far above AM's 4.5 kHz audio need —
the same margin argument the FM doc used to justify its own tb=15%
choice (FM doc §7.4). This is a **measured probe result**, not yet
implemented: `IfResampler`'s parameters are currently a single
compile-time constant pair shared by every mode
(`include/IfResampler.h:42-43`), so exploiting this would require
making them mode-aware (§9 below).

## 4. Effect of `dev-resampler-lowlatency` on AM

| Observable | dev (`ce34651`) | branch (`cde04d0`) | Δ |
|---|---|---|---|
| WAV frames (48 kHz) | 285621 | 287712 | +2091 |
| WAV duration | 5.950437 s | 5.994000 s | +43.563 ms |
| `IfResampler` deficit | 49.5625 ms | 6.0000 ms | **−43.5625 ms** |
| 1 kHz tone onset | 1001.146 ms | 1001.146 ms | 0 |

The onset is bit-identical between the two binaries (both measured at
1001.146 ms from file start, against a nominal 1000 ms onset — the
+1.146 ms is the downstream `AmDecoder` filter chain's own group delay
plus the finite rise time of the threshold-crossing detection method;
see §6 for why this undershoots the FIR's full linear-phase group
delay). This confirms — exactly as the FM doc found for FM (§9.2) —
that the resampler parameter change alters only the real-time
processing delay, not the content or timeline alignment of the
decoded audio.

**Why `AudioResampler` is irrelevant to AM** (already stated in §1,
repeated here since the task specifically calls it out): `AudioResampler`
is a private member of `FmDecoder` only. `AmDecoder` has no member of
that type and never calls it. `main.cpp` does construct an idle
`FmDecoder` (and hence an idle `AudioResampler` pair) even in AM mode
(`main.cpp:841-848`), but its `.process()` is only invoked when
`modtype == ModType::FM` (`main.cpp:981-985`), never for
`ModType::AM`. The `dev-resampler-lowlatency` branch's `AudioResampler`
parameter change (`08a5932`/`c33e03d`, 34.4 ms → 2.1 ms as-built,
FM doc §9.3) therefore has **zero** effect on AM reception — the
entire AM-side benefit of this branch comes from the `IfResampler`
change alone, measured above at −43.5625 ms.

For comparison, FM's own `IfResampler` benefit from the same branch
was −5.44 ms (6.19 → 0.75 ms, FM doc §9.3) — **AM's `IfResampler`
benefit (43.56 ms) is about 8x larger**, for the reason given in §3:
AM's output rate makes the same percentage-based transition band an
8x-narrower absolute band, so there was 8x more (accidental) slack to
recover.

## 5. Decode-quality verification

FFT of the settled 2.0–5.0 s segment (Hann window, 144000-sample FFT),
identical for both `dev` and branch (as expected — resampler
parameters affect only real-time delay, not content, confirmed in §4):

| Metric | dev | branch |
|---|---|---|
| Tone level (1000.0 Hz) | 76.22 dB(FS) | 76.22 dB(FS) |
| Worst spur (2000.0 Hz, 2nd harmonic) | 15.61 dB(FS) → **−60.61 dBc** | 15.61 dB(FS) → **−60.61 dBc** |

The decode is clean: the tone dominates the worst spur by 60.6 dB, and
the two binaries produce bit-identical spectra. The residual 2nd
harmonic is **not** a resampler artifact — it is unchanged between the
old and new resampler parameters, so it must originate elsewhere in
the fixed part of the chain. The most likely mechanism: `m_ifagc`
(`IfSimpleAgc`) is a **per-sample** adaptive gain control
(`sfmbase/IfSimpleAgc.cpp:36-51`), not a slow/decoupled AGC; at 50%
modulation depth and its default `rate = 0.0003`
(`sfmbase/AmDecode.cpp:71-77`), the instantaneous gain has a small
ripple synchronized to the modulating tone itself, which is exactly
the mechanism that generates a 2nd-harmonic-dominated distortion floor
around −60 dBc. Since it is identical across both binaries and around
60 dB below the fundamental (comparable to real analog AM receiver
performance), it does not compromise the latency comparison and the
test signal did not need to be revised.

## 6. Non-resampler AM latency contributors (onset-shift, not deficit)

Unlike `IfResampler`, these stages emit one output sample per input
sample (or are memoryless), so their delay shows up as a shift in the
output *timeline* (onset), not as a steady-state deficit held inside a
buffer.

| Stage | Type | Group delay | How obtained |
|---|---|---|---|
| `m_amfilter` (`jj1bdx_am_48khz_default`, 255 taps @ 48 kHz) | linear-phase FIR | `(255-1)/2 / 48000` = **2.6458 ms** | computed exact (tap count is exact; linear-phase FIR group delay is exact by construction) |
| `m_ifagc` (`IfSimpleAgc`) | per-sample adaptive gain | 0 (memoryless); ripple/settling is a distortion effect (§5), not a delay | source inspection |
| `demodulate_am` (envelope detector) | memoryless (`volk_32fc_magnitude_32f`) | 0 | source inspection |
| `m_dcblock` (`HighPassFilterIir`, cutoff 60 Hz) | 2nd-order IIR | **0.0136 ms** at 1 kHz (test tone); much larger near cutoff — see below | computed exact: numerical group-delay evaluation (`-d(phase)/dw`) of the as-built biquad transfer function (`sfmbase/Filter.cpp:254-290`) |
| `m_afagc` (`AfSimpleAgc`) | per-sample adaptive gain | 0 (memoryless) | source inspection |
| `m_deemph` (`LowPassFilterRC`, time constant 100 µs = 4.8 samples) | 1st-order IIR | **0.0616 ms** at 1 kHz | computed exact, same method (`sfmbase/Filter.cpp:186-188`) |
| **Total (linear-phase sum, at 1 kHz)** | | **2.7211 ms** | sum |

`m_dcblock`'s group delay is strongly frequency-dependent (it is a
high-pass filter, so its phase distortion concentrates near its own
60 Hz cutoff): the same exact evaluation gives 3.853 ms at 10 Hz,
3.751 ms at 60 Hz, 0.365 ms at 200 Hz, collapsing to 0.014 ms by
1000 Hz and 0.0007 ms at 4500 Hz (the top of the AM audio band). This
mirrors the FM doc's general point (§1 row 11) that IIR "group delay"
near a filter's own corner frequency is really a settling-time effect,
not a fixed number — quoting a single figure only makes sense at a
specific test frequency, which is why this document reports the 1 kHz
figure (the test tone) alongside the frequency sweep.

**Cross-check against the measured onset**: the sum of computed
linear-phase group delays at 1 kHz is 2.721 ms, but the measured onset
shift (§4) was only 1.146 ms in both binaries. This is expected, not a
discrepancy: "group delay" describes the delay of a sinusoid's phase
(equivalently, the shift of a signal's spectral centroid through the
filter), whereas the onset-detection method here finds the **first**
sample whose magnitude crosses a small threshold after a hard step —
a causal FIR's impulse response begins responding immediately (its
first tap is nonzero), so the leading edge of a step response arrives
measurably before the symmetric group delay of the steady-state tone
that follows it. The FM doc noted an analogous small excess in its own
onset figure (+1.375 ms vs. a similarly-computed FIR group delay,
attributed to "downstream pilot-cut FIR group delay plus tone
build-up," FM doc §9.2). The exact tap-count-derived 2.646 ms
`m_amfilter` figure, not the onset measurement, is the correct number
to use in the latency budget, since it describes the delay actually
experienced by the tone once the signal has settled (which is what
matters for continuous broadcast reception, as opposed to a single
onset transient).

Separately, the DC-blocker's own startup transient is visible in the
decoded WAV (both binaries): a decaying artifact from t=0 (a hard step
onto full carrier amplitude) with maximum amplitude ≈0.184 (arbitrary
units) at t=0, decaying to exactly 0 by t≈0.2 s. This is a one-time
startup effect (same category as the FM doc's §2 discussion of
`r8brain`'s internal discard and the multipath filter's warm-up
period), not a steady-state contributor, and does not affect the
onset or deficit measurements above (both used a settling window
before the true 1.0 s tone onset).

## 7. Output stage

Not remeasured here — the output stage (`PortAudioOutput`,
`sfmbase/AudioOutput.cpp`) is identical code for every modulation
type, driven by the same `audio_output->write()` call
(`main.cpp:1030`) regardless of `modtype`. The FM doc's §11 measurement
applies directly:

| Configuration | Requested | Granted `outputLatency` (measured, FM doc §11.2) |
|---|---|---|
| `dev` (`defaultHighOutputLatency`, 40 ms floor, `include/AudioOutput.h:112` `minimum_latency_default`) | 40.000 ms | **210.667 ms** |
| branch (`defaultLowOutputLatency`, 25 ms floor, `include/AudioOutput.h:111` `minimum_latency_low`, `sfmbase/AudioOutput.cpp:224-228`) | 25.000 ms | **110.333 ms** |

Device: FiiO K7 USB DAC, macOS/arm64, same machine as the FM
measurement. The current HEAD (`cde04d0`) additionally removes the
`paMacCoreChangeDeviceParameters` flag that `ec48dd6` had set — the
FM doc's own decomposition (§11.2, "request 25 ms, *no* flags: still
110.333 ms") already showed this flag has no measurable effect on the
granted latency for this device, so citing the 110.333 ms figure for
the current branch remains valid without remeasurement.

## 8. Workarounds (config-level, no code change)

Directly transferable from the FM doc's §4, since the mechanisms are
source-level and mode-independent:

| Change | Mechanism | Expected saving (AM) | Risk |
|---|---|---|---|
| Reduce RTL-SDR `blklen` from 16384 to 4096 | Shrinks §1's batching term | ~10.67 ms (14.22 → 3.56 ms) | Same as FM doc §4 row 1: shrinks async in-flight jitter tolerance, still ample headroom |
| Prefer Airspy HF+ at its native 384 kHz over RTL-SDR at 1152 kHz | For AM, `IfResampler` still engages (1152k→48k avoided, but 384k→48k is a *smaller* ratio, likely similar or lower r8brain group delay — not separately measured here) | Removes RTL's 14.22 ms batching term; resampler-side saving not measured for this device | Device-dependent, no software risk |
| For USB/LSB/CW/WSPR only: none of the extra fine-tuner/SSB-filter stages apply to broadcast AM reception | N/A — informational | N/A | N/A |

The output-stage floor (§7) is shared with FM and already reduced by
this branch; the FM doc's §4 row about `defaultLowOutputLatency`
headroom (110.3 ms → 7.25 ms achievable if the 25 ms floor itself were
dropped) applies identically here, pending the same on-air underrun
stress testing the FM doc calls for (§11.3).

## 9. Architectural changes (code-level), ranked by measured benefit vs. effort

1. **Make `IfResampler`'s `req_trans_band`/`req_atten` mode-aware, or
   pass them as constructor arguments instead of compile-time
   constants.** Currently a single pair of `static constexpr` values
   serves both FM's 384 kHz output and AM's 48 kHz output
   (`include/IfResampler.h:42-43`), which is why AM inherited a
   parameter tuned for FM's tighter constraints and is paying an 8x
   latency penalty relative to what its own 4.5 kHz audio bandwidth
   requires (§3). **Measured benefit:** at minimum 3.06 ms (adopting
   FM's own tb=15%/att=120 dB values, §3.1) up to 5.27 ms (tb=44%,
   att=100 dB, near r8brain's 45% transition-band cap) beyond the
   branch's current 6.00 ms AM figure. **Complexity:** low — this is
   the same class of change as `c33e03d` itself (an explicit
   constructor overload or a second constant pair selected by
   `ModType`), touching only `IfResampler`'s construction call in
   `main.cpp:798-800`. **Regression risk:** low for AM specifically
   (AM's audio bandwidth leaves far more margin than FM's own
   tb=15%/37% experiments needed, per the passband-edge column in
   §3.1), but must not be allowed to leak into the FM path's own
   parameters (keep the FM code path's values exactly as `c33e03d`
   left them) — needs the usual on-air listening verification since
   there is no automated test suite (`CLAUDE.md`).
2. **Everything in the FM doc's §4/§5 that is source- or
   output-stage-level** (blklen reduction, further output-latency-floor
   work) transfers unchanged, since those mechanisms are
   mode-independent; see §8 above and FM doc §5 items 1-2 for the
   detailed ranking. These now dominate AM's remaining budget (110.3 ms
   output + up to 14.2 ms batching, against ≤6 ms of resampler-side
   headroom), so — as for FM — **the output stage is the
   highest-value remaining target for AM too**, not further DSP-side
   AM tuning.
3. **Not recommended: touching `AmDecoder`'s FIR/IIR stages.** The
   255-tap `amfilter` contributes 2.646 ms (§6) and the two small IIRs
   contribute under 0.1 ms combined at the test tone — an order of
   magnitude below the output stage and below even the remaining
   `IfResampler` headroom in item 1. Shortening these would trade
   demodulation quality for negligible latency gain, the same
   conclusion the FM doc reached for its own pilot-cut FIR (FM doc §5
   item 6).

## 10. Summary table (all measured/computed figures)

| # | Term | dev | branch | Δ | Label |
|---|---|---|---|---|---|
| 1 | Source batching (test config, `blklen=2048`) | 1.778 ms | 1.778 ms | 0 | computed |
| 1b | Source batching (RTL-SDR representative, `blklen=16384`) | 14.222 ms | 14.222 ms | 0 | computed |
| 2 | `FourthConverterIQ` | 0 (test skips it) | 0 | 0 | measured (test config) / computed ~0 (RTL-SDR) |
| 3 | `IfResampler` (1152k→48k) | 49.5625 ms | 6.0000 ms | **−43.5625 ms** | measured |
| 4 | `AmDecoder` `m_amfilter` FIR | 2.6458 ms | 2.6458 ms | 0 | computed exact |
| 5 | `AmDecoder` `m_dcblock` + `m_deemph` IIR (@ 1 kHz) | 0.0752 ms | 0.0752 ms | 0 | computed exact |
| 6 | PortAudio granted output latency | 210.667 ms | 110.333 ms | **−100.334 ms** | measured (cited, FM doc §11) |
| | **Total (test config)** | **264.73 ms** | **120.83 ms** | **−143.90 ms** | sum |
| | **Total (RTL-SDR representative)** | **277.17 ms** | **133.28 ms** | **−143.90 ms** | sum |

The remaining, not-yet-implemented headroom identified in §3.1/§9 item
1 (mode-aware `IfResampler` parameters) would take the branch's
`IfResampler` term from 6.00 ms down to as low as 0.73 ms, i.e. a
further ~5.3 ms off the branch totals above (133.28 → ~128.0 ms
RTL-SDR representative) — small next to the 110.3 ms output stage, but
close to free given how directly `c33e03d`'s own precedent applies.

## Appendix: artifacts

All in the session scratchpad
(`/private/tmp/claude-501/-Users-kenji-src-airspy-fmradion/a226bec1-4666-4ad2-bdd4-55d31aaddbf4/scratchpad/`),
not in the repository:

- `gen_am_onset.py` — synthetic AM IQ generator.
- `am_onset_1152k_s16.raw` — the 6.0 s test file.
- `am_out_dev.wav`, `am_out_branch.wav` — decoded WAV outputs, §3-§5.
- `am_dbgout_branch.wav`, `am_dbglog_branch.txt` — debug-instrumented
  branch run (`-DDEBUG_IFRESAMPLER -DDEBUG_AUDIORESAMPLER`), §3.
- `r8b_am_deficit.cpp`, `r8b_am_deficit` — standalone no-defines
  r8brain probe for AM's exact rates/parameters, §3, §3.1.
- `am_iir_groupdelay.py` — exact numerical group-delay evaluation of
  `m_dcblock`/`m_deemph`'s as-built biquad/one-pole coefficients, §6.
- `build-dbg/` — the pre-existing debug CMake build directory
  (rebuilt, bit-identical to source, no code changed).
