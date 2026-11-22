---
name: build
description: Build airspy-fmradion with CMake — full clean build, incremental rebuild, optimization flags, optional MP3 feature, submodule dependencies, and the submodule check. Use when compiling the project, editing CMakeLists.txt, or investigating build errors.
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

## Submodule check

```sh
cmake --build build --target check_submodules
```

Fails when a submodule is not initialized, is not at its pinned commit, or
has local changes. It is not part of `all`. Configure runs the same check
and only warns; a missing submodule stops configure with an error. The check
is `cmake/CheckSubmodules.cmake`.

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

## Dependencies in git submodules

All three are git submodules at the repository root.

- `cmake-git-version-tracking` (jj1bdx fork) — builds the static library
  that implements the functions declared in `include/git.h`
  (`git::CommitSHA1()` etc.), from the git state of the airspy-fmradion
  checkout; added with `add_subdirectory`
- `fmt` (`{fmt}`) — used throughout for all formatted output (`fmt::print`,
  `fmt::println`); added with `add_subdirectory`, static library only
- `r8brain-free-src` — used header-only through the include path, with no
  `R8B_*` defines

To update a dependency, check out the new commit in the submodule (for
example `git -C fmt checkout <tag>`), build and test, then commit the
submodule path to record the new pin.
