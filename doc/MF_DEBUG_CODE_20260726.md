# Multipath filter debug code — review and fixes (2026-07-26)

`05edb03` added a `DEBUG_MULTIPATH_FILTER` compilation flag to
`sfmbase/MultipathFilter.cpp` that prints the filter order and the effective
alpha. This document records the review of that commit and the two defects
fixed in `6a8ced9`, on branch `dev-debug-flags` cut from `dev` at `05edb03`
and merged to `dev` as `911c210`.

## Executive summary

- **The flag itself is sound.** It compiles clean with `-Wall -std=c++20 -O3`,
  the `fmt` usage and the conditional include are correct, and printing the
  *effective* `m_alpha` — after the order scaling and the `alpha_maximum` clamp
  — is the right quantity, since both `MF_ALPHA` and `MF_ALPHA_MAX` are
  `-D`-overridable (§1).
- **Defect 1: the enable hint said the opposite of what it meant.** The
  commented-out line under *"Define this to print …"* read `#undef`, not
  `#define`. Uncommenting it disabled the flag, and it would also have
  overridden a `-D` passed through `EXTRA_FLAGS`. The same wrong line was
  present in two more files (§2).
- **Defect 2: the report printed for a filter that never runs.** `FmDecoder`
  constructs `MultipathFilter` unconditionally, with a dummy stage count of 1
  when the filter is off, so the constructor printed
  `filter_order: 5, alpha: 0.003448` on every run without `-E` (§3).
- **No release-build impact.** Everything involved is inside `#ifdef`; the
  default build is byte-for-byte unaffected apart from recompilation (§4).

## 1. What the flag does

`sfmbase/MultipathFilter.cpp` prints one line reporting the filter
configuration:

```
FM multipath filter configuration: filter_order: 145, alpha: 0.1
```

`m_filter_order` is `stages * 4 + 1`, and `m_alpha` is the value the adaptation
loop actually uses:

```cpp
m_alpha(std::min(alpha * static_cast<double>(m_filter_order) /
                     static_cast<double>(alpha_reference_order),
                 alpha_maximum))
```

Reporting `m_alpha` rather than the `alpha` constant is what makes the flag
useful: `MF_ALPHA` (0.1) and `MF_ALPHA_MAX` (0.5) both have `#ifndef` guards in
`include/MultipathFilter.h`, so either can be overridden at build time, and the
order scaling of `doc/MULTIPATH_FILTER_DESIGN_20260724.md` §15 then moves the
result again. The printed line is the only place the composed value is visible.

At the reference order (`stages = 36` → order 145 = `alpha_reference_order`)
the scaling is a no-op and the printed alpha is the unscaled 0.1, which is the
expected reading for the common `-E36` case.

## 2. Defect 1 — the enable hint was inverted

Three files carried the same pattern:

```cpp
// Define this to print multipath filter messages
// #undef DEBUG_MULTIPATH_FILTER
```

Uncommenting that line *undefines* the macro. As a comment it is inert, so
nothing was broken in a default build, but it is a trap in two ways: a reader
following the comment gets no output, and if the line were uncommented while
`-DDEBUG_MULTIPATH_FILTER` came in through `EXTRA_FLAGS`, the `#undef` would
silently win — it sits above the `#ifdef` that tests the macro.

The rest of the tree already uses the opposite convention
(`sfmbase/AirspySource.cpp:33`, `sfmbase/AirspyHFSource.cpp:32`:
`// #define DEBUG_AIRSPYSOURCE 1`). All three were corrected to match:

| file | flag | was | now |
|---|---|---|---|
| `sfmbase/MultipathFilter.cpp:39` | `DEBUG_MULTIPATH_FILTER` | `// #undef …` | `// #define DEBUG_MULTIPATH_FILTER 1` |
| `sfmbase/PilotPhaseLock.cpp:27` | `DEBUG_PLL_FILTER` | `// #undef …` | `// #define DEBUG_PLL_FILTER 1` |
| `main.cpp:53` | `COEFF_MONITOR` | `// #undef …` | `// #define COEFF_MONITOR 1` |

In each file the hint line precedes the `#ifdef` that consumes it, so
uncommenting now enables the flag as the comment promises.

## 3. Defect 2 — the report described a filter that never runs

`sfmbase/FmDecode.cpp:81` constructs the member unconditionally, substituting a
dummy stage count when the multipath filter is disabled:

```cpp
m_multipathfilter(m_enable_multipath_filter ? m_multipath_stages : 1)
```

`m_enable_multipath_filter` then gates every call to `process()`
(`FmDecode.cpp:114`). With the report in the constructor, a debug build without
`-E` printed

```
FM multipath filter configuration: filter_order: 5, alpha: 0.003448
```

for an object whose `process()` is never called — `0.1 * 5 / 145`, the scaled
alpha of the dummy. That reads as "the filter is configured with order 5",
which is precisely the wrong conclusion during a debugging session, and it
contradicts `main.cpp:900`, which prints its own
`FM IF multipath filter enabled, stages: N` line only when `-E` was given.

The report now runs from the first filtered block instead:

```cpp
bool MultipathFilter::process(const IQSampleVector &samples_in,
                              IQSampleVector &samples_out) {
#ifdef DEBUG_MULTIPATH_FILTER
  static bool config_reported = false;
  if (!config_reported) {
    config_reported = true;
    fmt::println(...);
  }
#endif // DEBUG_MULTIPATH_FILTER
```

`process()` is reached only while the filter is enabled, so the line appears
exactly when it is meaningful. It is also emitted after the 100-block warm-up
wait of `FmDecode.cpp:109`, i.e. when filtering genuinely starts.

**Why a function-local static, not a member.** A `bool` member declared inside
`#ifdef DEBUG_MULTIPATH_FILTER` would change the class layout depending on
whether the including translation unit was compiled with the macro — an ODR
violation, since `MultipathFilter.h` is pulled in by `FmDecode.h`. Declaring it
unconditionally instead leaves a member that is dead in release builds and
draws `-Wunused-private-field` under clang's `-Wall`. A static local in the
`.cpp` avoids both. Only one `MultipathFilter` instance exists in the program,
so per-instance state buys nothing.

## 4. Verification

- **Per-translation-unit compile**, each with its own flag defined, using the
  exact `build/compile_commands.json` command line plus `-D`: all three of
  `MultipathFilter.cpp` (`DEBUG_MULTIPATH_FILTER`), `PilotPhaseLock.cpp`
  (`DEBUG_PLL_FILTER`) and `main.cpp` (`COEFF_MONITOR`) compile with no
  warnings. Before this pass the flags-on paths were unexercised.
- **Default incremental build** of the tree: clean.
- **Runtime**, from a build configured with
  `-DEXTRA_FLAGS="-DDEBUG_MULTIPATH_FILTER=1"`, decoding
  `test-files/piano_iqtest-a0p5-t5us.wav` through `filesource`:

  | invocation | output |
  |---|---|
  | `-E36` | `FM IF multipath filter enabled, stages: 36` then `FM multipath filter configuration: filter_order: 145, alpha: 0.1` |
  | no `-E` | nothing (previously `filter_order: 5, alpha: 0.003448`) |

- **`clang-format --dry-run -Werror`** clean on all three edited files.

## 5. Enabling the flags

Either uncomment the hint line in the file, or inject the definition through
`EXTRA_FLAGS` — which is the `cmake` variable appended to `CMAKE_CXX_FLAGS` at
`CMakeLists.txt:231`, and the route to prefer, since it does not dirty the
working tree:

```sh
cmake -S . -B build-debug -DEXTRA_FLAGS="-DDEBUG_MULTIPATH_FILTER=1"
cmake --build build-debug --target all
```

Note that `EXTRA_FLAGS` applies to the whole build, so it reaches every
translation unit; `DEBUG_PLL_FILTER` and `COEFF_MONITOR` are far more verbose
than `DEBUG_MULTIPATH_FILTER` and are best enabled one at a time.

## 6. Not changed

- The `filter_order` / `alpha` message text and format (`{:.4g}`) are kept as
  written in `05edb03`.
- The conditional `#include <fmt/format.h>` is kept. `IfResampler.cpp` and
  `AudioResampler.cpp` include `fmt` unconditionally and use it only under
  their debug flags; the conditional form here is the tighter of the two and
  costs nothing.
- The dummy `stages = 1` construction at `FmDecode.cpp:81` is left alone. It is
  what makes `FmDecoder` hold the filter by value with no optional wrapper, and
  with the report moved it is no longer observable.
