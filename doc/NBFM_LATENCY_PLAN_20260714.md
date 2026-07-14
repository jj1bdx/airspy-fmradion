# NBFM (narrow-band FM) reception output latency analysis and reduction plan (20260714)

**Date:** 2026-07-14
**Author:** Claude Code (cpp-expert agent)
**Scope:** Where narrow-band FM (`-m nbfm`) reception latency comes from
in this codebase, how it compares to the already-analyzed FM, AM, and
CW paths, the measured effect of branch `dev-resampler-lowlatency` on
it, and ranked options to reduce it further. Every number in this
document is either **measured** (executable-level WAV-duration
deficit, a debug-instrumented build's `getInLenBeforeOutStart()`
print, a standalone no-defines r8brain probe, or a decoded-WAV FFT) or
**computed** (exact tap-count arithmetic for a linear-phase FIR's
group delay, or a numerically evaluated filter frequency response)
from the as-built source. No repository file was modified except this
one; all builds/signals used are in the session scratchpad.

Environment: macOS/arm64 (Mac mini 2023), same machine as the FM, AM,
and CW documents (`doc/LATENCY_PLAN_20260713.md`,
`doc/AM_LATENCY_PLAN_20260714.md`, `doc/CW_LATENCY_PLAN_20260714.md`).
Test source: `FileSource` with a synthetic NBFM IQ file,
`srate=1152000`, `raw`, `format=S16_LE`, default `blklen` (2048,
`include/FileSource.h:34`). Binaries compared: `dev` (`ce34651`,
pre-branch, `scratchpad/airspy-fmradion-dev`) and the current branch
`dev-resampler-lowlatency` at `2f0caa6`
(`/Users/kenji/src/airspy-fmradion/build/airspy-fmradion`, built from
`cde04d0`; no source file affecting the DSP chain changed between
`cde04d0` and `2f0caa6` — the intervening commits are documentation
only, confirmed by `git log --oneline` and by the fact that the
prebuilt binary and a fresh debug rebuild from the current tree
produce bit-identical `IfResampler` latency figures, §3).

## Executive summary

**NBFM total steady-state latency, current branch (HEAD), RTL-SDR-class
representative configuration (`srate=1152000`, `blklen=16384`):
≈132.51 ms**, versus **≈276.41 ms** on `dev` (`ce34651`) — a measured
reduction of **≈143.90 ms**. This is, to four significant figures, the
same reduction the AM and CW docs measured for their own paths, because
**NBFM's IF resampling is byte-for-byte the same code path, at the
same rates, with the same parameters, as AM/CW** — confirmed directly
below, not merely inferred from the AM/CW docs.

| Term | dev (`ce34651`) | branch (HEAD) | Δ | Label |
|---|---|---|---|---|
| Source batching (test config, `blklen=2048`) | 1.7778 ms | 1.7778 ms | 0 | computed |
| Source batching (RTL-SDR representative, `blklen=16384`) | 14.2222 ms | 14.2222 ms | 0 | computed |
| `IfResampler` (1152k→48k, NBFM/AM/CW-shared path) | 49.5625 ms | 6.0000 ms | **−43.5625 ms** | measured |
| `NbfmDecoder` `m_nbfmfilter` FIR (127 taps @ 48 kHz, default filter type) | 1.3125 ms | 1.3125 ms | 0 | computed exact |
| `NbfmDecoder` `m_audiofilter` FIR (63 taps @ 48 kHz) | 0.6458 ms | 0.6458 ms | 0 | computed exact |
| PortAudio granted output latency (FiiO K7) | 210.667 ms | 110.333 ms | **−100.334 ms** | measured (cited, FM doc §11) |
| **Total (test config)** | **263.966 ms** | **120.069 ms** | **−143.897 ms** | sum |
| **Total (RTL-SDR representative)** | **276.410 ms** | **132.513 ms** | **−143.897 ms** | sum |

Three findings are specific to NBFM and worth stating up front:

1. **NBFM's `IfResampler` numbers are not just analogous to AM/CW's —
   they are the identical code path, verified by direct measurement**
   (§3), not just cited: `nbfm_target_rate = NbfmDecoder::internal_rate_pcm`
   = 48000 (`main.cpp:686`, `include/NbfmDecode.h:34`), exactly like
   AM's `am_target_rate` and identically 48000 for CW. `main.cpp:727-728`
   sets `if_decimation_ratio = ifrate / nbfm_target_rate` in its own
   `switch` arm (distinct from the AM-family arm), but algebraically it
   is the same computation with the same target rate, so `IfResampler`
   at `srate=1152000` runs 1152k→48k for NBFM exactly as it does for
   AM/CW, using the same globally-shared `req_trans_band`/`req_atten`
   constants (`include/IfResampler.h:42-43`, one pair for every mode).
   The independent WAV-deficit measurement, debug-instrumented build,
   and standalone probe below all reproduce AM's exact 49.5625 ms
   (dev) / 6.0000 ms (branch) figures to the measurement's own
   precision.
2. **`NbfmDecoder` performs no rate conversion after `IfResampler` and
   has no `AudioResampler` member.** `sample_rate_pcm =
   internal_rate_pcm = 48000` (`include/NbfmDecode.h:33-34`); the
   decoder's own two FIR filters (`m_nbfmfilter`, `m_audiofilter`) both
   run at 48 kHz with no resampling between them (§1). As with AM/CW,
   an idle `FmDecoder` (and its private `AudioResampler` pair) is
   constructed unconditionally even in `-m nbfm` mode
   (`main.cpp:841-848`) — confirmed directly in this session's own
   debug build, which prints `AudioResampler latency = 809` twice at
   startup during an NBFM-mode run — but `.process()` on it is never
   invoked for `ModType::NBFM` (`main.cpp:980-1001`), so this branch's
   `AudioResampler` parameter change contributes **zero** to NBFM, the
   same conclusion the AM/CW docs reached for their own modes.
3. **NBFM has much less mode-aware `IfResampler` headroom than AM,
   because of the `-f wide` filter variant.** AM's own doc found large
   slack (audio bandwidth 4.5 kHz against a 24 kHz output Nyquist,
   §3.1 there). NBFM's `-f wide` filter
   (`jj1bdx_nbfm_48khz_wide`, `main.cpp:831`, intended for NOAA
   satellite reception at `freq_dev_wide = 17000` Hz per
   `include/NbfmDecode.h:39-40`, though `main.cpp:852` currently always
   constructs `NbfmDecoder` with `freq_dev_normal` regardless of
   `-f` — see §7) has a computed −100 dB edge at **19,878 Hz** (§6.2),
   only 1.72 kHz below the branch's current `IfResampler` passband
   edge (21.6 kHz, from `tb=10%` of the 24 kHz output Nyquist). Because
   `IfResampler`'s parameters are one global constant pair shared by
   every mode *and* every `-f` filter type, any further loosening
   (as AM safely did) would need to keep clearance above the *widest*
   NBFM filter's requirement, which severely caps the achievable
   saving — see §8 for the quantified headroom.

## 1. NBFM signal path walkthrough

`main.cpp`'s NBFM rate selection is its own `switch` arm, structurally
parallel to (but textually separate from) the AM-family arm the AM/CW
docs describe:

- `nbfm_target_rate = NbfmDecoder::internal_rate_pcm` = 48000
  (`main.cpp:686`, `include/NbfmDecode.h:34`).
- `if_decimation_ratio = ifrate / nbfm_target_rate` in the
  `case ModType::NBFM:` arm (`main.cpp:727-728`), separate from
  `case ModType::FM:` (`main.cpp:724-725`, uses `fm_target_rate` =
  `FmDecoder::sample_rate_if` = 384000) and separate from the
  AM-family arm (`main.cpp:730-737`, uses `am_target_rate` = 48000).
  NBFM and AM/CW/DSB/USB/LSB/WSPR reach the *same numeric* target rate
  (48000) via two different `switch` arms that happen to use the same
  constant value — not shared code, but identical arithmetic.
- `demodulator_rate = ifrate / if_decimation_ratio` (`main.cpp:747`),
  algebraically exactly 48000 Hz regardless of `ifrate`. Confirmed in
  this session's own run log for `srate=1152000`: `IF decimation: / 24`,
  `Demodulator rate: 48000 [Hz], audio decimation: / 1` — the identical
  numbers the AM/CW docs captured for their own modes.
- `IfResampler if_resampler(ifrate, demodulator_rate)`
  (`main.cpp:798-800`) is the **same single object** used for FM, AM,
  DSB, USB, LSB, CW, WSPR, and NBFM — there is exactly one
  `IfResampler` instance in the program, constructed once regardless of
  `modtype`, and it is what NBFM drives at `main.cpp:945-946`
  (`if_resampler.process(if_shifted_samples, if_samples)`), the same
  call site every mode uses.
- `enable_downsampling = (ifrate != demodulator_rate)`
  (`main.cpp:801`) — true for `srate=1152000` (1152000 ≠ 48000), so
  `IfResampler` engages.
- `FourthConverterIQ fourth_downconverter` (`main.cpp:796`) is
  engaged when `enable_fs_fourth_downconverter = !up_srcsdr->is_low_if()`
  is true (`main.cpp:680`), a source-level property independent of
  `modtype` — identical mechanism to the AM/CW docs' own discussion.
  `FileSource::is_low_if()` returns `!m_zero_offset`
  (`sfmbase/FileSource.cpp:291`), default `false` →
  `is_low_if()=true` → the fourth-converter is skipped for this
  document's `FileSource` test, exactly as in the FM/AM/CW docs' own
  tests, so the test baseband carrier sits at 0 Hz.
- `nbfmfilter_coeff` is selected by `-f` filter type
  (`main.cpp:808-833`); the default gives
  `FilterParameters::jj1bdx_nbfm_48khz_default`
  (`main.cpp:813`, 127 taps — §5).
- `NbfmDecoder nbfm(nbfmfilter_coeff, NbfmDecoder::freq_dev_normal)`
  (`main.cpp:851-853`) — **note:** `freq_dev` is always
  `freq_dev_normal` (8000 Hz) here, regardless of which `-f` filter
  type (including `wide`) is selected; only the filter coefficients
  vary with `-f`, not the phase discriminator's deviation
  normalization. This is a pre-existing characteristic of the code,
  not something this document's latency measurement changes; it is
  noted in §7 because it interacts with the `-f wide` headroom
  discussion in §8.
- Driven at `main.cpp:986-989`: `nbfm.process(if_samples, audiosamples)`,
  `if_rms = nbfm.get_if_rms()`, inside the `case ModType::NBFM:` arm.
- `audio_output->write(std::move(audiosamples))` (`main.cpp:1030`) —
  the same single output call every mode uses, at `pcmrate = 48000`
  (`main.cpp:283`), feeding the same `PortAudioOutput` class (§7 of
  this doc cites the FM doc's own measurement of that stage).

`NbfmDecoder::process()` (`sfmbase/NbfmDecode.cpp:47-96`) stage order —
notably **shorter** than `AmDecoder`'s (no DC blocker, no
de-emphasis, no separate audio-side AGC):

1. `m_nbfmfilter.process(samples_in, m_buf_filtered)` —
   `LowPassFilterFirIQ` built from `jj1bdx_nbfm_48khz_default`
   (`NbfmDecode.cpp:31`, `include/NbfmDecode.h:74`), downsample factor
   1, applied to the complex IQ signal.
2. `m_if_rms = Utility::rms_level_sample(m_buf_filtered)` — status-line
   bookkeeping only, no signal-path effect.
3. `m_ifagc.process(m_buf_filtered, m_samples_in_after_agc)` —
   `IfSimpleAgc`, constructed `(1.0, 100000.0, 0.0001)`
   (`NbfmDecode.cpp:43`): initial gain 1.0, max gain 100000 (+100 dB),
   rate 0.0001 — the **gentlest** rate of any mode in this codebase
   (AM's default is 0.0003, CW's is 0.0006, per the AM/CW docs).
   Memoryless in the group-delay sense, same classification as every
   other `IfSimpleAgc` instance in this codebase.
4. `m_phasedisc.process(m_samples_in_after_agc, m_buf_decoded)` —
   `PhaseDiscriminator`, constructed with
   `max_freq_dev = m_freq_dev / internal_rate_pcm = 8000/48000`
   (`NbfmDecode.cpp:35`). Implemented as
   `volk_32fc_s32f_atan2_32f` (instantaneous phase) followed by
   `volk_32f_s32f_32f_fm_detect_32f` (differencing against
   `m_save_value`, one retained sample) — a 1-sample-state operation,
   the same "≈0 ms, 1-sample state" classification the FM doc gives
   its own `PhaseDiscriminator` instance (FM doc §1 row 8); at 48 kHz
   this is 1/48000 = 0.0208 ms, negligible.
5. `volk_32f_convert_64f` — type conversion, memoryless.
6. `m_audiofilter.process(m_buf_baseband, m_buf_baseband_filtered)` —
   `LowPassFilterFirAudio` built from `jj1bdx_48khz_nbfmaudio`
   (`NbfmDecode.cpp:39`, `include/NbfmDecode.h:76`), 63 taps — §5.
7. `Utility::adjust_gain(m_buf_baseband_filtered, audio_gain)` — a
   fixed −3 dB (0.707×) gain multiply, memoryless
   (`NbfmDecode.cpp:90-92`).

There is **no DC blocker and no de-emphasis filter anywhere in
`NbfmDecoder`** — a structural difference from both `AmDecoder`
(DC-block + optional de-emphasis) and `FmDecoder` (de-emphasis +
pilot-cut FIR). NBFM's chain is the shortest of the four modes this
project's latency docs have now examined.

## 2. Measurement methodology

Reused, unmodified, the FM/AM/CW docs' validated methods.

- **Synthetic NBFM IQ generation** (`scratchpad/gen_nbfm_onset.py`):
  6.0 s, S16LE interleaved IQ, `srate=1152000`. Unmodulated carrier
  (constant baseband amplitude 20000, 0 Hz offset — the same
  "zero-beat"/`FourthConverterIQ`-skipped convention the AM/CW docs
  used) for `[0, 1.0)` s, then a 1 kHz tone frequency-modulating the
  carrier at **±5000 Hz peak deviation** for `[1.0, 6.0)` s. 5 kHz was
  chosen because it is the deviation the project's own header comment
  calls out as nominal (`include/NbfmDecode.h:36`: *"Full scale carrier
  frequency deviation for <=20kHz channel (deviation: +-5kHz
  nominal)"*) and sits safely inside both the phase discriminator's
  ±8000 Hz normalization range (`freq_dev_normal`) and the default IF
  filter's passband (§5/§6.2).
- **Decode command** (both binaries):
  ```
  airspy-fmradion -t filesource -m nbfm \
    -c "filename=nbfm_onset_1152k_s16.raw,srate=1152000,freq=145000000,raw,format=S16_LE" \
    -W nbfm_out.wav
  ```
  Confirmed in the run log: `Decoding modulation type: nbfm`, `IF
  decimation: / 24`, `Demodulator rate: 48000 [Hz]`, `Filter type:
  default`. `FileSource` paces blocks to real time
  (`sfmbase/FileSource.cpp:433-458`); each 6.0 s file took ≈6.02 s of
  wall-clock time to decode (`time` output, both binaries).
- **Output-duration deficit**: `6.000 s − WAV duration` — the
  resampler chain's steady-state group delay, per the FM doc's
  `DoConsumeLatency` analysis (§1.2 there); FIR/IIR stages downstream
  emit 1:1 and contribute nothing to it.
- **Onset**: first sample (scanning from 0.3 s) whose magnitude exceeds
  a 10%-of-settled-RMS threshold (`scratchpad/analyze_nbfm.py`).
- **Debug-instrumented build**: `cmake -S
  /Users/kenji/src/airspy-fmradion -B scratchpad/build-dbg
  -DEXTRA_FLAGS="-DDEBUG_AUDIORESAMPLER -DDEBUG_IFRESAMPLER"` (the
  scratchpad build directory already existed, configured for the
  current branch; rebuilt with `cmake --build scratchpad/build-dbg
  --target airspy-fmradion -j 8` — bit-identical to source, no code
  changed) and the pre-existing `scratchpad/dbg-dev` debug build of
  `dev` (`ce34651`, confirmed via its own printed Git commit SHA1).
  `NbfmDecode.cpp` has **no** `DEBUG_*`-style hooks of its own
  (checked: `grep -rn "DEBUG_" sfmbase/NbfmDecode.cpp` finds nothing),
  so the debug build's only useful print for NBFM is `IfResampler`'s —
  the same situation the AM doc found for `AmDecode.cpp`.
- **Standalone r8brain probe**: `scratchpad/r8b_am_deficit.cpp`
  (already built as `scratchpad/r8b_am_deficit`, from the AM
  investigation) was reused **without modification** — it targets
  1152000→48000 Hz with the exact `IfResampler` constructor arguments,
  and since NBFM's `demodulator_rate` is algebraically identical to
  AM's (§1), no new probe was needed; its output is cited directly in
  §3 as a third independent cross-check of this document's own
  WAV-deficit and debug-build measurements. Compiled with **no
  `R8B_*` defines** (`c++ -std=c++20 -O2
  -I/Users/kenji/src/airspy-fmradion/r8brain-free-src`), matching
  exactly how `sfmbase` compiles the header-only r8brain templates
  (FM doc §9.4).
- **Filter frequency response**: coefficients for
  `jj1bdx_nbfm_48khz_default/narrow/medium/wide` and
  `jj1bdx_48khz_nbfmaudio` extracted verbatim from
  `sfmbase/FilterParameters.cpp` by exact line range (cross-checked
  against `doc/filter-design/48kHz-nbfm-*-coeff.txt` and
  `48kHz-nbfmaudio-4kHz-63taps-coeff.txt` — first coefficients match
  to full double precision, confirming the mapping between design file
  and shipped array) and evaluated with `scipy.signal.freqz`
  (`scratchpad` inline script), §5/§6.2.
- **Quality check**: Hann-windowed FFT of a settled 3.0 s segment
  (2.0–5.0 s) of the decoded WAV, tone peak vs. worst non-tone
  spectral peak (`scratchpad/analyze_nbfm.py`).

## 3. Measured `IfResampler` latency for NBFM (both branches)

| Configuration | WAV deficit (measured) | Debug build `getInLenBeforeOutStart()` | Per-block accounting (debug build) | Standalone probe (no-defines, reused from AM) |
|---|---|---|---|---|
| `dev` (`CDSPResampler24` preset) | **49.5625 ms** | 57117 in-samples = 49.5807 ms | deficit 2379 out-samples = 49.5625 ms | 49.5625 ms |
| branch (HEAD) | **6.0000 ms** | 6927 in-samples = 6.0130 ms | deficit 288 out-samples = 6.0000 ms | 6.0000 ms |

All four independent methods (executable WAV deficit on the real NBFM
signal, the debug-instrumented executable's `getInLenBeforeOutStart()`
print, that same executable's own per-block sample accounting, and the
standalone r8brain probe run with no defines) agree to within 0.02 ms
— the same reconciliation tolerance the FM/AM/CW docs achieved. The
per-block accounting is exact: for the branch,
`total_in=6912000, total_out=287712, expected_out=288000.0,
deficit=288 samples`; 287712 matches the actual decoded WAV frame
count exactly.

**These numbers are numerically identical to the AM/CW docs' own
`IfResampler` measurements** (`doc/AM_LATENCY_PLAN_20260714.md` §3,
`doc/CW_LATENCY_PLAN_20260714.md` §4: 49.5625 ms dev, 6.0000 ms
branch) — this document independently reproduces them from a fresh
NBFM-specific test signal and decode, rather than assuming the code
path is shared, per the task's explicit measurement requirement.

## 4. Effect of `dev-resampler-lowlatency` on NBFM

| Observable | dev (`ce34651`) | branch (HEAD) | Δ |
|---|---|---|---|
| WAV frames (48 kHz) | 285621 | 287712 | +2091 |
| WAV duration | 5.950437 s | 5.994000 s | +43.563 ms |
| `IfResampler` deficit | 49.5625 ms | 6.0000 ms | **−43.5625 ms** |
| 1 kHz tone onset (10% threshold, nominal 1000 ms) | 1002.0000 ms | 1002.0000 ms | 0 |
| FFT tone peak | 1000.00 Hz | 1000.00 Hz | 0 |
| FFT worst spur | −46.26 dBc (5000 Hz) | −46.26 dBc (5000 Hz) | 0 |

The onset and spectrum are bit-identical between the two binaries —
exactly as the FM/AM/CW docs found for their own modes: the branch's
resampler-parameter change alters only real-time processing delay,
never the decoded content or timeline alignment.

**Why `AudioResampler` is irrelevant to NBFM** (stated in §1, repeated
here since the task specifically calls it out): `AudioResampler` is a
private member of `FmDecoder` only (`include/FmDecode.h`).
`NbfmDecoder` has no member of that type — confirmed directly from
`include/NbfmDecode.h:60-78`, which lists exactly
`m_nbfmfilter`, `m_phasedisc`, `m_audiofilter`, `m_ifagc` and no
resampler — and never calls it. `main.cpp` does construct an idle
`FmDecoder` (and hence an idle `AudioResampler` pair) even in NBFM
mode (`main.cpp:841-848`), confirmed directly: this session's own
`-DDEBUG_AUDIORESAMPLER` build prints `AudioResampler latency = 809`
(twice — mono and stereo instances) at startup during an `-m nbfm`
run, but the accompanying per-block `AudioResampler` process trace
never appears — only `IfResampler`'s does, exactly the AM doc's own
diagnostic (§1 there). The branch's `AudioResampler` parameter change
(34.4 ms → 2.1 ms as-built, FM doc §9.3) therefore has **zero** effect
on NBFM — the entire NBFM-side benefit of this branch flows through
the `IfResampler` change alone, measured above at **−43.5625 ms**,
identical to AM's and CW's own figure for the mechanistic reason given
in §1 (same target rate, same shared constants, same code path).

## 5. NBFM's own FIR filters (onset-shift, not deficit)

Unlike `IfResampler`, these stages emit one output sample per input
sample, so their delay shows up as a shift in the output *timeline*
(onset), not as a steady-state deficit held inside a buffer.

| Stage | Type | Taps | Group delay | How obtained |
|---|---|---|---|---|
| `m_nbfmfilter` (`jj1bdx_nbfm_48khz_default`, IQ, 48 kHz) | linear-phase FIR | 127 | `(127-1)/2 / 48000` = **1.3125 ms** | computed exact (tap count verified by exact line-range extraction from `sfmbase/FilterParameters.cpp:406-450`, symmetry confirmed: first coefficient equals last) |
| `m_ifagc` (`IfSimpleAgc`, rate 0.0001) | per-sample adaptive gain | — | 0 (memoryless) | source inspection |
| `m_phasedisc` (`PhaseDiscriminator`) | 1-sample-state differencer | — | ≈0.0208 ms (1/48000) | source inspection, same classification as FM doc §1 row 8 |
| `m_audiofilter` (`jj1bdx_48khz_nbfmaudio`, real, 48 kHz) | linear-phase FIR | 63 | `(63-1)/2 / 48000` = **0.6458 ms** | computed exact (`sfmbase/FilterParameters.cpp:71-92`, symmetry confirmed) |
| **Total (linear-phase FIR sum)** | | | **1.9583 ms** | sum |

Both tap counts were independently verified against
`doc/filter-design/48kHz-nbfm-10kHz-127taps-coeff.txt` and
`doc/filter-design/48kHz-nbfmaudio-4kHz-63taps-coeff.txt`: the first
coefficient of each design file matches the corresponding
`FilterParameters.cpp` array to full double precision (e.g.
`1.927771981761583710E-6` in the design file vs.
`1.9277719817615837e-06` in `jj1bdx_nbfm_48khz_default`), confirming
the mapping between the checked-in design artifact and the shipped
coefficients.

This total (1.96 ms) is far smaller than AM's `m_amfilter` alone
(2.65 ms, 255 taps) and two orders of magnitude smaller than CW's
`m_cwfilter` (21.33 ms, 2049 taps) — NBFM has the lightest FIR burden
of any mode examined in this document series, consistent with its
127-tap and 63-tap filters being the shortest linear-phase designs in
`FilterParameters.cpp` outside the FM pilot-cut filter.

An earlier regex-based extraction attempt for the same arrays produced
spurious tap counts (321 for the audio filter, 324 in a second
attempt) by over-matching past the intended array's closing brace;
this was caught by cross-checking against the design file's known tap
count in its own filename (`...-63taps-...`) and by the symmetry test
(first coefficient must equal the last for a Type-I linear-phase FIR),
and corrected to the exact-line-range method used for the table above.
This is noted here as a caution for anyone reproducing this
measurement with a similar script.

## 6. Decode-quality verification

### 6.1 Time-domain / spectral check

FFT of the settled 2.0–5.0 s segment (Hann window, 144000-sample FFT),
identical for both `dev` and branch (as expected — resampler
parameters affect only real-time delay, not content, confirmed in
§4):

| Metric | dev | branch |
|---|---|---|
| Tone level (1000.0 Hz) | 78.01 dB(FS) | 78.01 dB(FS) |
| Worst spur (5000.0 Hz, 5th harmonic of the 1 kHz tone) | 31.76 dB(FS) → **−46.26 dBc** | 31.76 dB(FS) → **−46.26 dBc** |

The decode is clean: the tone dominates the worst spur by 46.3 dB, and
the two binaries produce bit-identical spectra, confirming the
resampler-parameter change does not touch content. Unlike AM's
2nd-harmonic and CW's 3rd-harmonic spurs (both explained by `IfSimpleAgc`
gain ripple synchronized to an *amplitude-varying* envelope — AM's 50%
modulation depth, CW's keyed on/off carrier), NBFM's test signal has a
**constant-envelope** carrier (pure phase modulation, `|x|` = 20000 for
the whole tone-on interval), so `IfSimpleAgc`'s per-sample update
(`z = 1 + rate·(1 − |x·gain|²)`, `sfmbase/IfSimpleAgc.cpp:44`) should
converge to a steady gain with no periodic ripple from the AGC
mechanism itself. The most plausible source of the observed 5th
harmonic is instead **sideband clipping by `m_nbfmfilter`**: at 5 kHz
peak deviation and a 1 kHz tone, the FM modulation index is 5, so by
Carson's rule the occupied bandwidth is
`2×(Δf + f_tone) = 2×(5000+1000) = 12000` Hz — but the default IF
filter's −3 dB edge sits at only 8.4 kHz and its −100 dB edge at 9.9 kHz
(§6.2), so some outer Bessel sidebands of the modulated signal are
attenuated before the phase discriminator sees them, which for a
high-index tone test can reintroduce a small amount of envelope/phase
distortion. This is a **plausible mechanism, not a proven one** — it
was not chased further because it is identical on both binaries (so it
cannot be a resampler artifact) and −46 dBc is comparable to the CW
doc's own residual distortion floor, i.e. it does not compromise the
latency comparison this document exists to make.

### 6.2 IF filter frequency response (computed, all four `-f` variants)

Frequency response of all four `nbfmfilter_coeff` variants, evaluated
via `scipy.signal.freqz` against coefficients extracted by exact line
range from `sfmbase/FilterParameters.cpp` (127 taps each, verified
symmetric):

| Filter (`-f` type) | Design file (channel spacing) | −3 dB edge (computed) | −100 dB edge (computed) |
|---|---|---|---|
| `narrow` | `48kHz-nbfm-6.25kHz-127taps` | 4800 Hz | 6270 Hz |
| `medium` | `48kHz-nbfm-8kHz-127taps` | 6542 Hz | 8013 Hz |
| `default` | `48kHz-nbfm-10kHz-127taps` | 8399 Hz | 9936 Hz |
| `wide` | `48kHz-nbfm-20kHz-127taps` | 18242 Hz | 19878 Hz |

(Filenames give the design's target half-bandwidth in kHz; the
`default`/`medium`/`narrow`/`wide` array-to-file mapping was confirmed
by exact first-coefficient match, §5.) The `jj1bdx_48khz_nbfmaudio`
audio filter (63 taps, used identically for every `-f` type) has
−3 dB at ≈5.7 kHz and −100 dB by 9 kHz (from the same `freqz`
evaluation) — this is the filter that actually bounds NBFM's audible
output bandwidth, not the log line described next.

### 6.3 A cosmetic, non-latency-affecting quirk found while verifying decode quality

`main.cpp:856-864`'s status banner prints, for both `ModType::FM` and
`ModType::NBFM`:
```cpp
fmt::println(stderr, " audio bandwidth: {} [Hz]",
             (unsigned int)FmDecoder::bandwidth_pcm);
```
`FmDecoder::bandwidth_pcm` is `15000` (`include/FmDecode.h:44`) — FM
broadcast's own audio bandwidth. `NbfmDecoder` has **no**
`bandwidth_pcm` constant at all (confirmed: `grep -n bandwidth_pcm
include/NbfmDecode.h` finds nothing), so this line prints FM's 15 kHz
figure even when decoding NBFM, where the real audio bandwidth is the
`jj1bdx_48khz_nbfmaudio` filter's ≈5.7 kHz (§6.2) — confirmed in this
session's own run log: `audio bandwidth: 15000 [Hz]` appeared for both
binaries' `-m nbfm` runs. This is **purely a display string**; it does
not affect the DSP chain, any filter coefficient, or any latency
figure in this document, and both binaries print the same wrong value
identically. It is noted here as a minor, essentially free
documentation-accuracy fix a maintainer may want to make (print
`AmDecoder::bandwidth_pcm`-style per-mode text, or add a
`NbfmDecoder::bandwidth_pcm` constant derived from the audio filter's
actual passband), separate from anything this latency investigation
recommends acting on.

A second, related observability gap: `AmDecoder` exposes
`get_if_agc_current_gain()`, which `main.cpp:1088-1089` uses to print
an `AGC=...dB` field on the AM/CW/DSB/USB/LSB/WSPR status line
(`main.cpp:1090-1093`). `NbfmDecoder` has no equivalent accessor for
its own `m_ifagc` (`include/NbfmDecode.h:47-58` lists only
`get_tuning_offset`, `get_baseband_level`, `get_if_rms`), so NBFM's
internal AGC gain is not observable from the CLI at all
(`main.cpp:1074-1078`'s NBFM status line has no AGC term). This has no
effect on the latency figures in this document (§1 already classifies
`IfSimpleAgc` as memoryless/0 group-delay for every mode it appears
in), but is noted as a minor debugging-observability gap uncovered
while verifying the NBFM signal path.

## 7. Output stage

Not remeasured here — identical code and hardware to the FM/AM/CW
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
this device, and the current HEAD has removed that flag entirely per
`cde04d0`) and its caveat that this is PortAudio's own accounting, not
an acoustic loopback measurement.

## 8. Workarounds (config-level, no code change)

Directly transferable from the FM/AM/CW docs' own §4/§8/§10, since the
mechanisms are source-level and mode-independent:

| Change | Mechanism | Expected saving (NBFM) | Risk |
|---|---|---|---|
| Reduce RTL-SDR `blklen` from 16384 to 4096 | Shrinks source batching | ~10.67 ms (14.22 → 3.56 ms) | Same as FM doc §4 row 1 |
| Prefer Airspy HF+ at its native 384 kHz over RTL-SDR at 1152 kHz | `IfResampler` still engages (384k→48k rather than 1152k→48k) | Removes RTL's batching term; resampler-side saving not separately measured for this device | Device-dependent |
| Use `-f narrow` or `-f medium` instead of `-f default`/`-f wide` when channel conditions allow | No `IfResampler` effect (its parameters are global, §8.1); reduces `m_nbfmfilter`'s own passband, not its group delay (all four variants are 127 taps, so §5's 1.3125 ms figure is unchanged regardless of `-f`) | **0 ms** — included here only to state explicitly that filter *type* selection does not change NBFM's latency, only its selectivity | None — informational |

The output-stage floor (§7) is shared with FM/AM/CW and already
reduced by this branch; further headroom there (110.3 ms → 7.25 ms
achievable if the 25 ms floor itself were dropped, per the FM doc
§4/§11.3) applies identically to NBFM, pending the same on-air
underrun stress testing the FM doc calls for.

## 9. Architectural changes (code-level), ranked by measured/computed benefit vs. effort

### 9.1 Mode-aware `IfResampler` parameters: much less headroom than AM, because of `-f wide`

The AM doc (§3.1/§9 item 1) found large, safe headroom in loosening
`IfResampler`'s shared `req_trans_band`/`req_atten` constants because
AM's audio bandwidth (4.5 kHz) is tiny relative to the 24 kHz output
Nyquist. **NBFM does not have the same margin.** `IfResampler`'s
parameters are one pair of `static constexpr` values shared by every
mode *and* every `-f` filter type (`include/IfResampler.h:42-43`), so
any change must remain safe for the most demanding case actually
reachable through `-m nbfm`: `-f wide`, whose computed −100 dB edge is
**19,878 Hz** (§6.2) — the design's stated intent is NOAA satellite
reception at ±17 kHz deviation
(`include/NbfmDecode.h:38-40`, `freq_dev_wide`), a signal that
genuinely occupies most of the available IF bandwidth.

The branch's current `tb=10%` gives an `IfResampler` passband edge of
`24000×(1−0.10) = 21,600` Hz (same formula the AM doc used, §3.1
there) — only **1.72 kHz** of clearance above `-f wide`'s −100 dB
requirement. Reusing the AM doc's own measured probe table
(`scratchpad/r8b_am_deficit`, identical rates/constructor, §3):

| Params | Deficit (measured, reused from AM doc §3.1) | Passband edge (computed) | Clearance above `-f wide`'s 19,878 Hz need |
|---|---|---|---|
| tb=10.0%, att=140 dB (branch, current) | 6.0000 ms | 21,600 Hz | 1,722 Hz |
| tb=15.0%, att=120 dB (AM's own fix) | 2.9375 ms | 20,400 Hz | 522 Hz |
| tb=37.0%, att=120 dB | 1.5417 ms | 15,120 Hz | **−4,758 Hz (would clip `-f wide`)** |
| tb=44.0%, att=100 dB (AM's most aggressive) | 0.7292 ms | 13,440 Hz | **−6,438 Hz (would clip `-f wide`)** |

**Only `tb=15%` retains any margin at all for `-f wide`, and that
margin (522 Hz) is thin enough that it would need on-air/spectral
verification specifically with `-f wide` before adoption, not just the
`default`-filter listening test the AM doc's own tb=15% recommendation
called for.** Every more aggressive setting in the AM doc's own table
would clip `-f wide`'s passband outright.

If mode-aware (and, here, filter-type-aware) parameterization were
pursued, two shapes are possible:

1. **Conservative, NBFM-wide-safe: `tb=15%/att=120`** (matching AM's
   own choice). **Measured benefit:** 6.00 → 2.94 ms, saving
   **3.06 ms** — but only 522 Hz of computed clearance for `-f wide`,
   so this needs dedicated `-f wide` verification (synthetic NOAA-like
   signal or real satellite pass) before shipping, not just a
   `default`-filter listening check.
2. **`-f`-conditional parameters** (loosen only when `-f` is
   `narrow`/`medium`/`default`, keep the current safe `tb=10%` for
   `wide`): would require `IfResampler`'s parameters to become a
   function of *both* `modtype` and `filtertype`, not just `modtype`
   — meaningfully more complex than the AM doc's proposed
   mode-only change (`include/IfResampler.h:42-43` would need to
   become a constructor argument selected in `main.cpp`'s existing
   `switch (filtertype)` block, `main.cpp:808-833`, and threaded
   through the `IfResampler` construction at `main.cpp:798-800`,
   which currently happens *before* that switch runs — a modest
   reordering). **Estimated benefit:** up to the AM doc's own
   `narrow`/`medium`/`default` headroom (§6.2 shows even `default`'s
   −100 dB edge, 9936 Hz, has enormous clearance under any of the AM
   doc's tb values) for three of the four filter types, while leaving
   `wide` exactly as safe as today. **Complexity:** moderate — more
   than AM's single-mode change, less than a new filter class.
   **Regression risk:** low if `wide` is excluded, but the interaction
   between "IF resampler headroom" and "IF filter selectivity" is now
   two-dimensional (mode × filter type) instead of one, adding a
   category of future bug (a new `-f` variant added later without
   re-checking `IfResampler` clearance) that AM's simpler fix does not
   have.

**Recommendation:** given the thin/negative margins above, this is
**lower priority for NBFM than the equivalent change was for AM** —
the achievable saving (3.06 ms at best, conservatively) is small next
to the 110.3 ms output stage, and the `-f wide` interaction adds real
verification burden. Not recommended as a near-term change; if
pursued, start from option 1 (`tb=15%`) with explicit `-f wide`
on-air/spectral verification, not option 2's added complexity, unless
`-f wide` usage is common enough to justify it.

### 9.2 Everything mode-independent

The FM/AM/CW docs' own §4/§5/§8/§9 items that are source- or
output-stage-level (blklen reduction, further output-latency-floor
work) transfer unchanged, since those mechanisms are mode-independent.
The output stage (110.3 ms) remains the single largest term in NBFM's
total budget (§7); source batching (14.2 ms at RTL-SDR defaults) is
the second largest addressable term and is identical in mechanism to
every other mode.

### 9.3 Not recommended: touching `m_nbfmfilter`/`m_audiofilter`

At 1.3125 ms and 0.6458 ms respectively (§5), NBFM's own FIR stages
are already the lightest of any mode examined in this document series
— an order of magnitude below even AM's single `m_amfilter` (2.65 ms)
and nearly two orders below CW's `m_cwfilter` (21.33 ms). There is no
meaningful latency to recover here; any change would trade selectivity
for a saving in the tens-of-microseconds range, not worth the
engineering or on-air-verification effort.

### 9.4 Trivial, non-latency fixes noted in passing (§6.3)

Correcting the NBFM status line's audio-bandwidth display (currently
prints FM's 15000 Hz constant) and adding an `IfSimpleAgc`
gain-observability accessor to `NbfmDecoder` (mirroring
`AmDecoder::get_if_agc_current_gain()`) are both small, low-risk,
**zero-latency-effect** changes that would improve debuggability and
documentation accuracy. They are listed here only because they were
discovered during this investigation, not because they belong in a
latency-reduction ranking.

## 10. Summary table (all measured/computed figures)

| # | Term | dev | branch | Δ | Label |
|---|---|---|---|---|---|
| 1 | Source batching (test config, `blklen=2048`) | 1.7778 ms | 1.7778 ms | 0 | computed |
| 1b | Source batching (RTL-SDR representative, `blklen=16384`) | 14.2222 ms | 14.2222 ms | 0 | computed |
| 2 | `IfResampler` (1152k→48k, NBFM/AM/CW-shared path) | 49.5625 ms | 6.0000 ms | **−43.5625 ms** | measured |
| 3 | `NbfmDecoder` `m_nbfmfilter` FIR (127 taps, default) | 1.3125 ms | 1.3125 ms | 0 | computed exact |
| 4 | `NbfmDecoder` `m_audiofilter` FIR (63 taps) | 0.6458 ms | 0.6458 ms | 0 | computed exact |
| 5 | PortAudio granted output latency | 210.667 ms | 110.333 ms | **−100.334 ms** | measured (cited, FM doc §11) |
| | **Total (test config)** | **263.966 ms** | **120.069 ms** | **−143.897 ms** | sum |
| | **Total (RTL-SDR representative)** | **276.410 ms** | **132.513 ms** | **−143.897 ms** | sum |

Not-yet-implemented headroom identified in §9.1, with its `-f wide`
caveat:

| Change | Group delay after | Saving vs. branch's current 6.00 ms | Constraint |
|---|---|---|---|
| `tb=15%/att=120` (AM's own fix, reused) | 2.9375 ms | −3.0625 ms | Only 522 Hz computed clearance for `-f wide`; needs dedicated verification |
| `tb=37%` or higher | ≤1.5417 ms | ≥−4.4583 ms | **Computed to clip `-f wide`'s passband — not viable without also excluding `wide`** |

## Appendix: artifacts

All in the session scratchpad
(`/private/tmp/claude-501/-Users-kenji-src-airspy-fmradion/a226bec1-4666-4ad2-bdd4-55d31aaddbf4/scratchpad/`),
not in the repository:

- `gen_nbfm_onset.py`, `nbfm_onset_1152k_s16.raw` — synthetic NBFM
  test file/generator (§2), 6.0 s, 1 kHz tone at ±5 kHz deviation
  onset at 1.0 s.
- `nbfm_out_dev.wav`, `nbfm_out_branch.wav` — decoded WAV outputs,
  §3–§6.
- `nbfm_log_dev.txt`, `nbfm_log_branch.txt` — full run logs (both
  binaries), confirming `IF decimation: / 24`, `Demodulator rate:
  48000 [Hz]`, `Filter type: default`.
- `analyze_nbfm.py` — WAV-level deficit/onset/FFT analysis script.
- `nbfm_dbgout.wav`, `nbfm_dbglog.txt` (branch) and
  `nbfm_dbgout_dev.wav`, `nbfm_dbglog_dev.txt` (dev) —
  debug-instrumented (`-DDEBUG_IFRESAMPLER -DDEBUG_AUDIORESAMPLER`)
  runs, §3, confirming `IfResampler latency = 6927` (branch) /
  `57117` (dev) and exact per-block sample accounting.
- `r8b_am_deficit.cpp`, `r8b_am_deficit` — standalone no-defines
  r8brain probe (from the AM investigation, reused unmodified since
  NBFM's `IfResampler` rates/parameters are identical to AM's, §2/§3).
- `build-dbg/` — the pre-existing debug CMake build directory for the
  current branch (rebuilt, bit-identical to source, no code changed);
  `dbg-dev` — the pre-existing debug build of `dev`.
- Inline Python (not saved as separate files, reproduced in this
  document's §5/§6.2 text) — exact-line-range coefficient extraction
  and `scipy.signal.freqz` evaluation of
  `jj1bdx_nbfm_48khz_default/narrow/medium/wide` and
  `jj1bdx_48khz_nbfmaudio` from `sfmbase/FilterParameters.cpp`.
