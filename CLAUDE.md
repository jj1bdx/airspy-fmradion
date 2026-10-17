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

Mannered prose substitutes metaphor and flourish for direct statement. Instead of "a parameter worth varying," the mannered writer produces "a dial worth turning." Instead of "this point still matters," they write "this point earns its keep." The phrases exist to display the writer, not to convey the idea, and readers can tell. That is why mannered prose irritates: it makes the reader work harder so the writer can perform. It is also imprecise. Metaphors drag in connotations the writer did not choose and cannot control. The fix is to say what you mean. When a literal phrase is available, use it. Please remove all mannered prose.

## Testing

There are no automated tests; correctness is verified by running the binary
against an SDR device or file source.
