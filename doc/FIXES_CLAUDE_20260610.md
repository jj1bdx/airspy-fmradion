# Vulnerability Fixes (Third Pass) — airspy-fmradion

**Date:** 2026-06-10
**Author:** Claude Code (claude-fable-5)
**Scope:** Fixes for vulnerabilities W1–W7 from `VULNERABILITY_REPORT.md`
(2026-05-29 audit) and X1–X8 from `VULNERABILITY_REPORT_20260610.md`
(2026-06-10 audit). The prior remediations V1–V41 are documented in
`FIXES_CLAUDE_20260502.md` and `FIXES_CLAUDE_20260504.md`.

## Summary

| ID | File(s) | Severity | Fix |
|----|---------|----------|-----|
| W1 | `sfmbase/AirspyHFSource.cpp` | Medium | `stop()` now guards `airspyhf_stop` with `if (m_dev)` and `join()/reset()` with `if (m_thread)`, mirroring `AirspySource::stop()` |
| W2 | `sfmbase/AirspyHFSource.cpp` | Low | `hf_att` range validated on the parsed `int` (`attlevel < 0 || attlevel > 8`) **before** narrowing to `uint8_t`; the dead `< 0` test on the `uint8_t` is gone |
| W3 | `sfmbase/AirspyHFSource.cpp` | Low | Frequency lower bound enforced: `frequency < 1000` rejected. **Per explicit user instruction the lower bound is 1 kHz (1000 Hz), not the 192 kHz mentioned in the report**; the usage text in `main.cpp` already documents "1k to 31M, and 60M to 260M" |
| W4 | `sfmbase/AirspyHFSource.cpp` | Low | Private `configure(int sampleRateIndex, …)` rejects `sampleRateIndex < 0` or `>= m_srates.size()` with `m_error = "Invalid sample rate index"` before indexing |
| W5 | `sfmbase/AirspySource.cpp`, `sfmbase/AirspyHFSource.cpp` | Low | Both `callback()` methods now guard `m_buf->push(...)` with `if (m_buf)` |
| W6 | `sfmbase/FmDecode.cpp` | Informational | `mono_to_left_right`, `stereo_to_left_right`, `zero_to_left_right` compute `n`, `2 * n`, and the loop index in `size_t` instead of `unsigned int` |
| W7 | `main.cpp` | Informational | Exit cleanup block now calls `fclose(ppsfile)` when `ppsfile` is non-null and not `stdout` |
| X1 | `sfmbase/RtlSdrSource.cpp`, `sfmbase/FileSource.cpp` | Medium | `blklen` upper limits tightened to `IfResampler::max_input_length` (65536) at configuration time, with a clamp warning, so no source block can exceed the resampler chain limits (see rationale below) |
| X2 | `main.cpp` | Low | `-r` rejects `fabs(ppm) >= 1000000.0` (exact −100 % no longer multiplies `ifrate` by zero); the effective `ifrate` is checked `> 0` after compensation; an additional guard rejects IF upsampling ratios above 64x (see note below) |
| X3 | `sfmbase/AirspySource.cpp` | Low | Same defensive `sampleRateIndex` bounds check as W4, in the sibling class |
| X4 | `sfmbase/AirspySource.cpp` | Informational | `AirspySource::start()` ends with `return true;` instead of `return *this;`, consistent with all sibling sources |
| X5 | `sfmbase/AudioOutput.cpp` | Informational | `PortAudioOutput::output_close()` is now idempotent (`m_closed` checked on entry and set before any early return) and skips all PortAudio API calls when `m_zombie` is set or `m_stream == nullptr`, so no PortAudio API can be called after `Pa_Terminate()` |
| X6 | `main.cpp` | Informational | The main loop checks the return value of `audio_output->write()`; on failure it prints `audio_output->error()`, sets `stop_flag`, and exits the loop cleanly (normal cleanup path runs) |
| X7 | `include/DataBuffer.h` | Informational | `DataBuffer::push()` drops the **oldest** queued block beyond `max_queue_blocks = 1024`, counts drops in `m_dropped_blocks` (accessor `dropped_blocks()`), and prints a rate-limited warning (first drop and every 100th) |
| X8 | `main.cpp` | Informational | The ppm display computation (`get_tuning_offset() / tuner_freq`) is guarded by `tuner_freq > 0.0`, so `filesource` with `freq=0` no longer produces NaN/Inf in the status line |

## Files modified

### Headers

- `include/DataBuffer.h` — bounded queue (X7): `max_queue_blocks = 1024`
  constant, drop-oldest overflow policy in `push()`, `m_dropped_blocks`
  counter with `dropped_blocks()` accessor, rate-limited stderr warning via
  `{fmt}`.

### Sources

- `sfmbase/AirspyHFSource.cpp` — guarded `stop()` (W1); `hf_att` validated
  before narrowing (W2); frequency lower bound `>= 1000` Hz (W3);
  `sampleRateIndex` bounds check in the private `configure()` (W4);
  `if (m_buf)` guard in `callback()` (W5). The misleading
  `"AirspyHFSource::run:"` prefix in the `stop()` error message was also
  corrected to `"AirspyHFSource::stop:"`.
- `sfmbase/AirspySource.cpp` — `sampleRateIndex` bounds check in the private
  `configure()` (X3); `return true;` in `start()` (X4); `if (m_buf)` guard in
  `callback()` (W5).
- `sfmbase/FmDecode.cpp` — `size_t` size arithmetic in the three
  channel-expansion helpers (W6).
- `sfmbase/RtlSdrSource.cpp` — `blklen` clamp upper bound lowered from
  `1024 * 1024` to `IfResampler::max_input_length` (65536) with an
  "adjusted from {} to {}" warning when the configured value is changed (X1);
  added `#include "IfResampler.h"`.
- `sfmbase/FileSource.cpp` — after the existing 10 ms (`max_expected_us`)
  clamp, `m_block_length` is additionally capped at
  `IfResampler::max_input_length` with a clamp warning (X1); the defensive
  `kMaxBlockSamples` bound in `get_sf_read_float()` was tightened from
  `1 << 24` to `IfResampler::max_input_length` to match; added
  `#include "IfResampler.h"`.
- `sfmbase/AudioOutput.cpp` — `PortAudioOutput::output_close()` hardened (X5)
  as described above.
- `main.cpp` — `-r` endpoint rejection and `ifrate > 0` check plus the 64x
  upsampling-ratio guard (X2); write-failure handling in the main loop (X6);
  `tuner_freq > 0.0` guard for the ppm feed (X8); `fclose(ppsfile)` in the
  exit cleanup block (W7); the `-r` usage text now states that the
  ±1000000 ppm range is exclusive.

## Specific notes

### W3 — user-specified 1 kHz lower bound

The 2026-05-29 report recommended enforcing the *then-documented* 192 kHz
floor. **The user explicitly overrode this: the lower bound must be 1 kHz
(1000 Hz).** The usage text in `main.cpp` (Airspy HF `freq` description) had
already been updated by the user to read "1k to 31M, and 60M to 260M"; the
validation added to `AirspyHFSource::configure(std::string)` rejects
`frequency < 1000` to match. `freq=0` is therefore now rejected on both the
low-IF and zero-IF paths.

### X1 — block-length limit rationale

The resampler chain imposes two hard per-call limits:
`IfResampler::max_input_length = 65536` and
`AudioResampler::max_input_length = 32768` (both enforced by `assert`, which
is active in the default build but compiled out under `-DNDEBUG`, where an
oversized block corrupts r8brain heap buffers). `FourthConverterIQ` is 1:1,
so the source block size is exactly the `IfResampler` input size. The chosen
configuration-time limits guarantee both constraints:

- **RTL-SDR:** `blklen` is clamped to `[4096, 65536]`. The valid RTL-SDR
  sample-rate range is `[900001, 3200000]` Hz, so the IF resampler always
  *downsamples* (worst ratio 900001/384000 ≈ 2.34); a 65536-sample block
  therefore shrinks to at most ~27963 samples at the demodulator rate,
  within the 32768 audio-path limit. The downsampling-disabled case
  (`ifrate == demodulator_rate`) cannot occur because 384000/48000 Hz are
  outside the accepted srate range.
- **FileSource:** the pre-existing `max_expected_us` clamp limits any block
  to 10 ms of samples (`srate/100`), which bounds the decoder-side block to
  at most 10 ms at the demodulator rate (≤ 3840 samples) regardless of
  whether the IF resampler upsamples, downsamples, or is bypassed. The only
  remaining gap was sample rates above 6.5536 MHz, where 10 ms exceeds
  65536 input samples; the new cap at `IfResampler::max_input_length`
  closes it. (The previous report reproducer `srate=20M, blklen=200000` now
  clamps to 65536 and runs instead of aborting.)
- **Airspy/Airspy HF:** block sizes are device-driven (not user
  configurable) and well within the limits; no change.

The style follows the existing FileSource clamp behavior (clamp + stderr
warning) rather than rejecting, since out-of-range `blklen` was historically
clamped, not rejected.

### X2 — degenerate and pathological `-r` values

Three layers were added in `main.cpp`:

1. Option parsing rejects `fabs(ppm) >= 1000000.0` (previously `>`), so
   `-r -1000000` (exactly −100 %) can no longer zero `ifrate`. The usage
   text now notes the range is exclusive.
2. After IF rate compensation, `if (!(ifrate > 0))` exits with a clear
   error; this also covers a hypothetical device reporting a zero sample
   rate.
3. **Additional hardening beyond the report:** verification revealed that
   extreme-but-accepted values such as `-r -999999` (ifrate becomes
   0.384 Hz) crash with SIGSEGV inside r8brain, whose internal `int` length
   arithmetic overflows at a 10^6 upsampling ratio. A guard now rejects
   `demodulator_rate / ifrate > 64.0` with a clear error. 64x is far above
   any legitimate configuration (real upsampling cases such as a 48 kHz
   FileSource decoded as FM need only 8x).

### X7 — queue bound rationale

Normal real-time operation keeps the queue near empty (the main loop pulls a
block per iteration), so a cap only matters when the consumer persistently
falls behind. `max_queue_blocks = 1024` allows several seconds of buffering
at the supported sources' block rates (~100–400 blocks/s ⇒ ~2.5–10 s) —
orders of magnitude above transient scheduling jitter, so the cap cannot
disturb normal operation — while bounding worst-case memory (Airspy R2:
65536 samples × 8 bytes × 1024 blocks = 512 MiB). Drop-oldest was chosen so
that, on overload, the decoder keeps working on the most recent (real-time
relevant) samples. Drops are counted and reported to stderr (first drop and
every 100th) to make the condition visible without log spam.

### X8

The minimal display-side guard (`tuner_freq > 0.0`) was chosen, as suggested
by the report; `FileSource` continues to accept `freq=0` (the value is
informational for file decoding).

## Verification

A full clean rebuild (`rm -rf build; cmake -S . -B build; cmake --build
build --target all`) succeeded after all changes. (The previous `build/`
tree referenced a since-upgraded Homebrew libusb path and had to be
reconfigured.) The only remaining compiler warnings (`unused variable
'ssr'` / `'dsr'`) originate from the unmodified `r8brain-free-src`
submodule.

`clang-format -i main.cpp include/*.h sfmbase/*.cpp` was applied; `git diff
--stat` confirms only the eight intended files changed.

Behavioral checks (no SDR hardware available; device-attached paths such as
the RTL-SDR `blklen` clamp, the Airspy HF `stop()`/callback guards, and the
PortAudio close path could not be exercised and were verified by review):

- `-r -1000000` and `-r 1000000` → `ERROR: Invalid argument for -r`.
- `-r -999999` with a filesource → previously SIGSEGV (confirmed with lldb:
  crash in r8brain `memmove`); now exits with
  `ERROR: IF sample rate 0.384 [Hz] is too low for demodulator rate 384000 [Hz]`.
- `-r -100` with a filesource → runs and terminates normally.
- `filesource` with `srate=20000000, blklen=131072` (the X1 reproducer
  class) → previously hit `assert(input_size <= max_input_length)`; now
  prints `FileSource::configure: large blklen, clamp blklen 131072 to 65536`
  and runs to completion.
- `filesource` with `srate=384k, blklen=131072` → existing 10 ms clamp still
  takes precedence (`round blklen 131072 to 2048`).
- `filesource` with `freq=0` → status line shows `ppm= +0.000` (previously
  NaN).
- `-T /tmp/pps.txt` run to normal termination → PPS file written and closed
  via the new `fclose` path.

No automated tests exist in this repository; final correctness should be
validated by running the binary against an SDR device per the project
documentation.
