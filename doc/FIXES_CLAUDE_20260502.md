# Vulnerability Fixes — airspy-fmradion

**Date:** 2026-05-02
**Author:** Claude Code (claude-opus-4-7)
**Scope:** Fixes for vulnerabilities V1–V24 from `VULNERABILITY_REPORT.md`. V25 was intentionally skipped at user request.

## Summary

| ID  | File(s) | Severity | Fix |
|-----|---------|----------|-----|
| V1  | `main.cpp` | Critical | Replaced null-deref `!(up_srcsdr)` with `!(*up_srcsdr)` (operator bool) |
| V2  | `main.cpp` | Low | `tuner_freq != freq` → `std::fabs(tuner_freq - freq) > 1.0` |
| V3  | `sfmbase/AirspySource.cpp`, `include/AirspySource.h` | High | `int len = sample_count*2` → `std::size_t`; callback signature updated |
| V4  | `sfmbase/AirspySource.cpp`, `include/AirspySource.h` | Medium | `m_this` → `std::atomic<AirspySource*>`, accesses via `.load()` |
| V5  | `sfmbase/RtlSdrSource.cpp` | Informational | Explicit cast and comment for offset-binary 8-bit decoding |
| V6  | `sfmbase/RtlSdrSource.cpp` | High | Introduced `USB_STRING_MAX = 256` named constant with documentation |
| V7  | `sfmbase/RtlSdrSource.cpp` | Medium | Replaced `resize()` + `clear()` with `reserve()` |
| V8  | `sfmbase/RtlSdrSource.cpp`, `include/RtlSdrSource.h` | Medium | `m_this` → atomic; `run()` / `get_samples()` use loaded `self` |
| V9  | `sfmbase/FileSource.cpp` | High | Early-return when `m_sample_rate == 0` |
| V10 | `sfmbase/FileSource.cpp` | Medium | `assert(0)` → `std::abort()` (also added `<cassert>` and `<cstdlib>` includes) |
| V11 | `sfmbase/FileSource.cpp`, `include/FileSource.h` | Medium | `m_this` → atomic; `run()` / `get_samples()` / `get_sf_read_float()` use loaded `self` |
| V12 | `sfmbase/AirspyHFSource.cpp`, `include/AirspyHFSource.h` | High | Same overflow fix as V3 |
| V13 | `sfmbase/AirspySource.cpp`, `sfmbase/AirspyHFSource.cpp` | High | Added `return` after negative-`ndev` warning; cast to `std::size_t` for `resize` |
| V14 | `sfmbase/AirspySource.cpp`, `sfmbase/AirspyHFSource.cpp` | High | Bounds-check `dev_index` against `m_serials.size()` after re-enumeration |
| V15 | `sfmbase/AirspyHFSource.cpp` | Medium | Validate `frequency - 0.25*Fs ∈ [0, UINT32_MAX]` before cast |
| V16 | `main.cpp` | Medium | Longopts: `optional_argument` → `required_argument` for m, t, c, f, P, r |
| V17 | `main.cpp` | Medium | `stat_rate = std::max(1u, ...)` to guarantee non-zero divisor |
| V18 | `sfmbase/Filter.cpp` | High | `i < n - 1` → `i + 1 < n` in `process_interleaved` and `process_interleaved_inplace` |
| V19 | `sfmbase/Filter.cpp` | Medium | `m_order = coeff.empty() ? 0 : coeff.size() - 1`; added `assert(!coeff.empty())` |
| V20 | `sfmbase/AirspySource.cpp` | Low | Pass `frequency` (uint32_t) directly; removed redundant `double tuner_freq` round-trip |
| V21 | `include/Utility.h` | Low | Early-return on empty input in `rms_level_sample` and `samples_mean_rms` |
| V22 | `sfmbase/AudioOutput.cpp` | Medium | Check `sf_command(SFC_GET_LOG_INFO, ...)` `<= 0` before string construction |
| V23 | `sfmbase/FileSource.cpp` | Medium | Reject `block_length <= 0` from user config |
| V24 | `sfmbase/AirspySource.cpp`, `sfmbase/AirspyHFSource.cpp`, headers | Low | `m_srates` is now `std::vector<std::uint32_t>`; removed signed/unsigned compare |
| V25 | — | — | **Not applied** (excluded per user instruction) |

## Files modified

### Headers

- `include/AirspySource.h` — added `<atomic>`; `m_this` is `std::atomic<AirspySource*>`; `m_srates` is `std::vector<std::uint32_t>`; `callback` takes `std::size_t len`.
- `include/AirspyHFSource.h` — same atomic / vector / callback changes.
- `include/RtlSdrSource.h` — added `<atomic>`; `m_this` is `std::atomic<RtlSdrSource*>`.
- `include/FileSource.h` — added `<atomic>`; `m_this` is `std::atomic<FileSource*>`.
- `include/Utility.h` — empty-input guards in `rms_level_sample` and `samples_mean_rms`.

### Sources

- `main.cpp` — added `<algorithm>`, `<cmath>` includes; longopts switched to `required_argument`; null-pointer dereference fixed; epsilon comparison; `stat_rate` clamped to `>= 1`.
- `sfmbase/AirspySource.cpp` — atomic `m_this` definition; bounds check after re-enumeration; `get_device_names` early-return on negative `ndev`; size_t in `rx_callback`; pass `frequency` as `uint32_t`; m_srates uint32_t comparison.
- `sfmbase/AirspyHFSource.cpp` — atomic `m_this`; bounds check; early-return; size_t overflow fix; Fs/4 underflow guard; uint32_t comparison in `check_sampleRateIndex`.
- `sfmbase/RtlSdrSource.cpp` — atomic `m_this`; named buffer constant; `reserve` instead of `resize`+`clear`; explicit casts in sample decoding; `run()` / `get_samples()` use loaded pointer.
- `sfmbase/FileSource.cpp` — atomic `m_this`; zero-rate guard; `block_length <= 0` rejection; `assert(0)` → `std::abort()`; `run()` / `get_samples()` / `get_sf_read_float()` use loaded pointer.
- `sfmbase/Filter.cpp` — empty-coefficient guards in two FIR constructors; safe loop bound `i + 1 < n` in both `LowPassFilterRC` interleaved variants.
- `sfmbase/AudioOutput.cpp` — guard against negative `sf_command(SFC_GET_LOG_INFO, ...)` return.

## Verification

`cmake --build build --target all` succeeded after all changes. The only remaining compiler warnings (`unused variable 'ssr'` / `'dsr'`) originate from the unmodified `r8brain-free-src` submodule.

`clang-format -i` was applied to all modified files using the project's `.clang-format` configuration.

No automated tests exist in this repository; correctness should be validated by running the binary against an SDR device or file source per the project documentation.
