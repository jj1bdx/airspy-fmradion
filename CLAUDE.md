# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

Detailed guidance lives in project skills under `.claude/skills/`, loaded on
demand:

- `build` — CMake build commands, optimization flags, optional MP3 feature,
  CMake-fetched dependencies
- `format` — clang-format / cmake-format usage and style files
- `architecture` — signal flow, key type aliases, Source abstraction,
  FM decoder pipeline
- `commit-messages` — commit message convention

## Critical constraint

**Never add `-ffast-math`** to `OPTIMIZATION_FLAGS`. It enables
`-menable-no-nans`, which silently breaks the multipath filter's abnormality
detection (checked in `MultipathFilter`). The current flags are
`-O3 -ftree-vectorize`.

Use only American English spelling for documentation.

## Testing

There are no automated tests; correctness is verified by running the binary
against an SDR device or file source.
