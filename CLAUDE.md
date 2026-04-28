# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```sh
# Full clean build
/bin/rm -rf build
mkdir build
git submodule update --init --recursive
cmake -S . -B build
cmake --build build --target all
```

Incremental build after changes:
```sh
cmake --build build --target all
```

There are no automated tests; correctness is verified by running the binary against an SDR device or file source.

## Code formatting

```sh
# Format C++ sources
clang-format -i main.cpp include/*.h sfmbase/*.cpp

# Format CMakeLists.txt
cmake-format -i CMakeLists.txt
```

The `.clang-format` file defines the style (LLVM base, C++20, 80-column limit, 2-space indent, no tabs). The `.cmake-format.py` file defines CMake style.

## Architecture

### Signal flow

```
SDR hardware / file
    ↓
Source subclass (AirspySource / AirspyHFSource / RtlSdrSource / FileSource)
    ↓  [separate thread, pushes IQSampleVector]
DataBuffer<IQSample>  ← thread-safe queue with condition variable
    ↓  [main thread pulls]
FourthConverterIQ     ← Fs/4 downconversion for zero-IF devices (Airspy HF+, RTL-SDR)
    ↓
IfResampler           ← r8brain-based rational resampler to decoder's target rate
    ↓
FmDecoder / AmDecoder / NbfmDecoder
    ↓  [SampleVector at 48 kHz]
AudioOutput (SndfileOutput or PortAudioOutput)
```

### Key type aliases (include/SoftFM.h)

| Alias | Underlying type | Use |
|---|---|---|
| `IQSample` | `std::complex<float>` | Raw IF samples |
| `IQSampleVector` | `std::vector<IQSample>` | Block of IF samples |
| `IQSampleDecoded` | `float` | Phase-discriminated baseband sample |
| `Sample` | `double` | Audio sample |
| `SampleVector` | `std::vector<double>` | Audio block |
| `IQSampleCoeff` | `std::vector<float>` | FIR filter coefficients for IQ path |

Enums also live in `SoftFM.h`: `FilterType`, `DevType`, `ModType`, `OutputMode`, `PilotState`.

### Source abstraction

All SDR frontends implement the abstract `Source` interface (`include/Source.h`):
- `configure(string)` — parse key=value config string and open device
- `start(DataBuffer<IQSample>*, atomic_bool*)` — begin streaming in a background thread
- `stop()` / `is_low_if()` / `get_sample_rate()` / `get_frequency()`

Low-IF sources (Airspy R2/Mini output real low-IF, not zero-IF) skip the `FourthConverterIQ` step — controlled by `is_low_if()`.

### FM decoder pipeline (sfmbase/FmDecode.cpp)

Inside `FmDecoder::process()`:
1. Optional IQ bandpass filter (`LowPassFilterFirIQ`)
2. IF AGC (`IfSimpleAgc`)
3. Optional multipath filter (`MultipathFilter` — normalized LMS, enabled with `-E`)
4. Phase discriminator → baseband
5. Audio resampler (r8brain via `AudioResampler`)
6. Pilot PLL for stereo detection (`PilotPhaseLock`, also generates PPS events)
7. Stereo L−R demodulation, de-emphasis, DC blocking

### Optimization constraints

**Never add `-ffast-math`** to `OPTIMIZATION_FLAGS`. It enables `-menable-no-nans`, which silently breaks the multipath filter's abnormality detection (checked in `MultipathFilter`). The current flags are `-O3 -ftree-vectorize`.

VOLK (`libvolk`) is used for vectorized float operations (e.g., `volk_64f_convert_32f`). Run `volk_profile` once after installation to generate device-optimized kernel selection.

### Build-time optional feature

MP3 output (`-C` flag / `OutputMode::MP3_FMAUDIO`) is conditionally compiled only when libsndfile ≥ 1.1 is detected. The macro `LIBSNDFILE_MP3_ENABLED` guards all related code.

### External dependencies fetched by CMake

- `cmake-git-version-tracking` (jj1bdx fork) — provides `include/git.h` with `git::CommitSHA1()` etc.
- `{fmt}` — used throughout for all formatted output (`fmt::print`, `fmt::println`)
- `r8brain-free-src` — git submodule at repo root, compiled as static `libr8b`

### Commit message convention

First line: `Verb + object` or `Target: action` (e.g., `FmDecode: remove redundant copy`). Keep it short; details go after the third line.
