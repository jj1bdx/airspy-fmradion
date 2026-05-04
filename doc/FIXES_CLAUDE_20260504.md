# Vulnerability Fixes (Second Pass) — airspy-fmradion

**Date:** 2026-05-04
**Author:** Claude Code (claude-opus-4-7)
**Scope:** Fixes for vulnerabilities V26–V41 from `VULNERABILITY_REPORT.md`. The first-pass remediations V1–V24 are already documented in `FIXES.md` (V25 was intentionally skipped per user instruction in that pass).

## Summary

| ID  | File(s) | Severity | Fix |
|-----|---------|----------|-----|
| V26 | `include/AudioOutput.h` | High | Default-initialized `m_fd = -1`, `m_sndfile = nullptr`, `m_sndfile_sfinfo{}` so the destructor's `output_close()` is safe on early-construction failure |
| V27 | `sfmbase/AudioOutput.cpp` | High | Close the opened fd on `sf_format_check` and `sf_open_fd` failure paths; pass `nullptr` to `sf_strerror` (the variable is guaranteed null at that point) |
| V28 | `sfmbase/AirspySource.cpp`, `sfmbase/AirspyHFSource.cpp` | High | Initialize `nbSampleRates = 0` and check the return value of both `airspy(hf)_get_samplerates` calls; on failure close the device and surface an error |
| V29 | `sfmbase/FileSource.cpp` | Medium | Added explicit `kMaxBlockSamples = 1 << 24` upper bound; widened the `m_block_length * 2` multiply to `sf_count_t` |
| V30 | `sfmbase/FileSource.cpp` | Medium | Switched cadence clock from `std::chrono::system_clock` to `std::chrono::steady_clock`; explicitly typed all clock values (no `using` directive); clamp negative sleep durations and reset `begin` so subsequent iterations don't accumulate debt |
| V31 | `sfmbase/RtlSdrSource.cpp` | Medium | Replaced `double tuner_freq = frequency - sample_rate / 4.0` with integer arithmetic (`uint32_t shift = sample_rate / 4u`); explicit `frequency < shift` check rejects out-of-range cases |
| V32 | `include/AirspyHFSource.h`, `include/RtlSdrSource.h`, `include/FileSource.h` | Medium | Added in-class default initializers for `m_low_if = false`, `m_confAgc = false`, `m_sample_rate_per_us = 0.0` |
| V33 | `include/Source.h` | Medium | Base-class constructor now initializes `m_buf(nullptr)` and `m_stop_flag(nullptr)` |
| V34 | `main.cpp`, `sfmbase/MultipathFilter.cpp` | Low | Reject `-E` outside `[1, 1024]`; added `assert(stages < UINT_MAX / 4)` in `MultipathFilter` constructor (with `<cassert>` and `<climits>` includes) |
| V35 | `include/Utility.h` | Low | `parse_dbl` now resets and checks `errno == ERANGE` and `std::isfinite(v)` before and after unit-suffix multiplication; `parse_int` checks `errno == ERANGE` after `strtol` |
| V36 | `sfmbase/AirspySource.cpp` | Low | `AirspySource::stop()` now calls `airspy_stop_rx(m_dev)` before `m_thread->join()`, mirroring `AirspyHFSource::stop()` and removing the deadlock for non-SIGINT shutdown paths |
| V37 | `include/AudioOutput.h` | Low | Default-initialized `PaStreamParameters m_outputparams{}`, `PaStream *m_stream = nullptr`, `PaError m_paerror = paNoError` |
| V38 | `sfmbase/Filter.cpp` | Informational | Moved `if (n == 0)` empty-input check **before** the `samples_out.resize(...)` in `LowPassFilterFirIQ::process` and `LowPassFilterFirAudio::process`, eliminating the latent unsigned-underflow path |
| V39 | `include/MovingAverage.h` | Informational | Added `assert(!m_history.empty())` to `feed()` and `average()` (with `<cassert>` include) |
| V40 | `include/PilotPhaseLock.h` | Informational | `erase_first_pps_event()` now guards on `!m_pps_events.empty()` |
| V41 | `include/FmDecode.h`, `sfmbase/FmDecode.cpp`, `include/AmDecode.h`, `sfmbase/AmDecode.cpp` | Informational | Changed parameter type of `FmDecoder::process` and `AmDecoder::process` from `const IQSampleVector &` to `IQSampleVector` (by-value), so `m_samples_in_iffiltered = std::move(samples_in)` and `m_buf_filtered2 = std::move(samples_in)` actually move instead of silently copying |

## Files modified

### Headers

- `include/AudioOutput.h` — default-initialize `SndfileOutput::{m_fd, m_sndfile, m_sndfile_sfinfo}` and `PortAudioOutput::{m_outputparams, m_stream, m_paerror}`.
- `include/AirspyHFSource.h` — `m_low_if = false` default initializer.
- `include/RtlSdrSource.h` — `m_confAgc = false` default initializer.
- `include/FileSource.h` — `m_sample_rate_per_us = 0.0` default initializer.
- `include/Source.h` — base-class constructor initializes `m_buf(nullptr)` and `m_stop_flag(nullptr)`.
- `include/Utility.h` — added `<cerrno>` and `<cmath>` includes; `parse_dbl` and `parse_int` now reject `ERANGE` and non-finite values.
- `include/MovingAverage.h` — added `<cassert>` include; `feed()` / `average()` guarded by `assert(!m_history.empty())`.
- `include/PilotPhaseLock.h` — `erase_first_pps_event()` guarded by `!m_pps_events.empty()`.
- `include/FmDecode.h` — `FmDecoder::process` takes `IQSampleVector samples_in` by value.
- `include/AmDecode.h` — `AmDecoder::process` takes `IQSampleVector samples_in` by value.

### Sources

- `main.cpp` — `-E` argument validation tightened to `[1, 1024]`.
- `sfmbase/AudioOutput.cpp` — close `m_fd` (when not `STDOUT_FILENO`) on `sf_format_check` and `sf_open_fd` failure paths; pass `nullptr` to `sf_strerror` in the `sf_open_fd` failure branch.
- `sfmbase/AirspySource.cpp` — initialize `nbSampleRates = 0`, check return values of both `airspy_get_samplerates` calls, close the device on failure; `stop()` now calls `airspy_stop_rx` before `join()`.
- `sfmbase/AirspyHFSource.cpp` — same pattern as `AirspySource.cpp` for the sample-rate query.
- `sfmbase/FileSource.cpp` — `get_sf_read_float` enforces `kMaxBlockSamples` upper bound and widens the multiply to `sf_count_t`; `run()` switched from `system_clock` to `std::chrono::steady_clock` with explicitly-typed `time_point` / `duration` and a non-negative-sleep clamp.
- `sfmbase/RtlSdrSource.cpp` — Fs/4 frequency shift computed as `uint32_t`; rejects `frequency < shift` cases.
- `sfmbase/Filter.cpp` — empty-input check moved before `samples_out.resize` in `LowPassFilterFirIQ::process` and `LowPassFilterFirAudio::process`.
- `sfmbase/MultipathFilter.cpp` — added `<cassert>` and `<climits>` includes; assert `stages < UINT_MAX / 4` to forestall constructor overflow.
- `sfmbase/FmDecode.cpp` — `process` parameter is now `IQSampleVector` (by-value); the `std::move(samples_in)` on the no-IF-filter path now performs an actual move.
- `sfmbase/AmDecode.cpp` — same change for `AmDecoder::process`.

## Specific notes from user instruction

### V30

The user explicitly required:

- **No `using` directive.** All `std::chrono::steady_clock` references are spelled out fully.
- **Explicit type for `std::chrono::steady_clock::now()`.** The result is bound to a `std::chrono::steady_clock::time_point` (not `auto`).
- **Explicit type for values derived from steady_clock.** The duration of `end - begin` is bound to a `std::chrono::steady_clock::duration` (not `auto`); the converted sleep amount is `std::chrono::microseconds`.

The clamp logic also resets `begin` to `now() - expected` when the iteration ran over time, which prevents the throttle from being permanently disabled by a single slow block.

### V41

The user explicitly required: **change the parameter type of `process()` functions if necessary.**

Before the fix, `void FmDecoder::process(const IQSampleVector &samples_in, ...)` had `m_samples_in_iffiltered = std::move(samples_in);` inside the body. `std::move` on a `const &` produces a `const &&`, which the `vector::operator=(vector&&)` overload cannot bind to, so `vector::operator=(const vector&)` was selected and the operation silently degraded to a copy.

The parameter was changed to `IQSampleVector samples_in` (by-value) in both `FmDecoder::process` and `AmDecoder::process`. Inside the body, `std::move(samples_in)` now resolves to `IQSampleVector &&` and the move-assignment overload is selected as intended.

`NbfmDecoder::process` was **not** changed because its body never moves `samples_in` — only member-resident vectors are moved into `audio` in that decoder.

The call sites in `main.cpp` (`fm.process(if_samples, audiosamples)` and `am.process(if_samples, audiosamples)`) were left as-is. They now pass by value, incurring one copy at the boundary; this is unchanged from the previous behavior (the old `const &` followed by an internal copy was the same cost). Callers can opt in to a true move with `std::move(if_samples)` if they want to give up ownership at the call site.

## Verification

`cmake --build build --target all` succeeded after all changes. The only remaining compiler warnings (`unused variable 'ssr'` / `'dsr'`) originate from the unmodified `r8brain-free-src` submodule.

`clang-format -i` was applied to all modified `.cpp` and `.h` files using the project's `.clang-format` configuration.

No automated tests exist in this repository; correctness should be validated by running the binary against an SDR device or file source per the project documentation.
