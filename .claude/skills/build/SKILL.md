---
name: build
description: Build airspy-fmradion with CMake — full clean build, incremental rebuild, optimization flags, optional MP3 feature, and CMake-fetched dependencies. Use when compiling the project, editing CMakeLists.txt, or investigating build errors.
---

# Building airspy-fmradion

## Full clean build

```sh
/bin/rm -rf build
mkdir build
git submodule update --init --recursive
cmake -S . -B build
cmake --build build --target all
```

## Incremental build after changes

```sh
cmake --build build --target all
```

There are no automated tests; correctness is verified by running the binary
against an SDR device or file source.

## Optimization constraints

**Never add `-ffast-math`** to `OPTIMIZATION_FLAGS` (see CLAUDE.md — it
silently breaks the multipath filter). The current flags are
`-O3 -ftree-vectorize`.

VOLK (`libvolk`) is used for vectorized float operations (e.g.,
`volk_64f_convert_32f`). Run `volk_profile` once after installation to
generate device-optimized kernel selection.

## Build-time optional feature

MP3 output (`-C` flag / `OutputMode::MP3_FMAUDIO`) is conditionally compiled
only when libsndfile ≥ 1.1 is detected. The macro `LIBSNDFILE_MP3_ENABLED`
guards all related code.

## External dependencies fetched by CMake

- `cmake-git-version-tracking` (jj1bdx fork) — provides `include/git.h` with
  `git::CommitSHA1()` etc.
- `{fmt}` — used throughout for all formatted output (`fmt::print`,
  `fmt::println`)
- `r8brain-free-src` — git submodule at repo root, compiled as static `libr8b`
