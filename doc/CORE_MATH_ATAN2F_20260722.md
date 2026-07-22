# Replacing `fast_atan2f()` in `PilotPhaseLock` with CORE-MATH `atan2` (binary64)

**Date:** 2026-07-22
**Branch/commit at analysis:** `dev` @ `1cd146e`
**Subject:** `Utility::fast_atan2f()` as used by `PilotPhaseLock::process()`
(`sfmbase/PilotPhaseLock.cpp:105`)
**Candidate replacement:** CORE-MATH correctly-rounded `atan2` for two
`binary64` (double) values —
<https://core-math.gitlabpages.inria.fr/> , source
`src/binary64/atan2/atan2.c` (function `cr_atan2`), MIT-licensed,
© 2024–2025 Paul Zimmermann and Alexei Sibidanov.
**Test host:** Apple M2 Pro (arm64), macOS 26.5.2, Homebrew clang 22.1.8,
build flags `-O3 -ftree-vectorize` (the project's `OPTIMIZATION_FLAGS`).

---

## Executive summary

* The PLL calls `fast_atan2f()` **once per input sample** (384 kHz) to convert
  the biquad-filtered I/Q phasor into a phase error. It is the only
  transcendental in the inner loop besides `sin`/`cos`.
* CORE-MATH `cr_atan2` is a **correctly-rounded** double-precision `atan2`. Dropped
  into the loop it is **≈8 orders of magnitude more accurate** than the current
  float table lookup, at a cost of **+2.2 ns/call (≈+30 %)** in the PLL's locked
  operating regime.
* **That accuracy cannot be used by the PLL.** A faithful replay of the exact
  loop shows swapping `fast_atan2f → cr_atan2` moves the tracked pilot frequency
  by **rms 3×10⁻⁷ Hz (max 1.5×10⁻⁶ Hz)** and the 38 kHz subcarrier phase by
  **max 1.0×10⁻⁷ rad** — five orders of magnitude below the loop's own
  measured phase noise (0.0024 rad rms, ±0.044 Hz). The audible result is
  identical.
* The **+30 % cost is itself negligible** in absolute terms: +0.86 ms of CPU per
  second of audio, ≈0.09 % of one core.
* **Unexpected finding:** on this hardware the *system* `std::atan2` (double)
  is both **faster than `fast_atan2f` (4.56 ns vs 7.38 ns)** *and* effectively
  correctly rounded. `fast_atan2f` no longer earns its name on modern
  out-of-order FPUs; it was a win on the early-2000s embedded targets it was
  written for.

**Recommendation:** adopt `cr_atan2` **only if bit-for-bit reproducibility of
the pilot phase across platforms/libms is a project goal** — that is the one
thing it provides that nothing else does. If the goal is merely "more accurate
than the table," the one-line change to the already-commented-out
`std::atan2()` is simpler, faster here, and equally accurate. Neither change is
audible.

---

## Contents

1. [The call site and what it needs](#1-the-call-site-and-what-it-needs)
2. [The two functions](#2-the-two-functions)
3. [How the replacement would be integrated](#3-how-the-replacement-would-be-integrated)
4. [Measurement method](#4-measurement-method)
5. [Accuracy results](#5-accuracy-results)
6. [Speed results](#6-speed-results)
7. [End-to-end effect on the PLL](#7-end-to-end-effect-on-the-pll)
8. [Cost/benefit and recommendation](#8-costbenefit-and-recommendation)
9. [Reproduction](#9-reproduction)

---

## 1. The call site and what it needs

`sfmbase/PilotPhaseLock.cpp:98–105`:

```cpp
double new_phasor_i = m_biquad_phasor_i1.process(phasor_i);
double new_phasor_q = m_biquad_phasor_q1.process(phasor_q);
// double phase_err = std::atan2(new_phasor_q, new_phasor_i);
// Use float atan2 for fast and light-weight phase detection.
double phase_err = Utility::fast_atan2f(new_phasor_q, new_phasor_i);
```

Observations that frame the whole comparison:

* Both arguments are **`double`**. The current code **narrows them to `float`**
  before the table lookup, so `fast_atan2f` throws away precision *twice*:
  once at the `double→float` cast, once at the 257-entry table interpolation.
* The result is stored back into a **`double`** accumulator chain
  (`m_first_phase_err`, `m_freq`, `m_phase`), which the code comments and the
  header (`include/PilotPhaseLock.h:81–84`) explicitly keep in double precision
  because "float rounding here directly becomes 38 kHz subcarrier phase noise."
  So the loop *wants* precision at exactly this point, yet the phase detector is
  the one float step in an otherwise all-double recursion.
* In the **locked** state the operating point is `x = new_phasor_i ≈ +0.104`
  (the pilot level), `y = new_phasor_q` small, `|phase_err|` rms ≈ 0.0024 rad,
  max ≈ 0.016 rad (from the empirical verification in
  `doc/PLL_ANALYSIS_20260722.md`). `x` is essentially always positive and
  `|y| ≪ x`, so only a thin slice of `atan2`'s domain is ever exercised.

The `std::atan2` on the commented line and `fast_atan2f` are therefore two
already-considered options; CORE-MATH is a third.

## 2. The two functions

| | `fast_atan2f` (current) | CORE-MATH `cr_atan2` |
|---|---|---|
| Origin | GNU Radio, table + linear interp | INRIA CORE-MATH, correctly-rounded |
| Type | `float` | `double` (binary64) |
| Method | 257-entry `atan` table, 45° folding, linear interpolation | rational core + double-double fast path; falls back to a 192-bit "triple-int" (`tint.h`) accurate path only when the fast path can't round safely |
| Source size | ~145 lines + 257-entry table (`include/Utility.h:158–302`) | `atan2.c` 586 lines + `tint.h` 543 lines |
| Extra deps | none | `<fenv.h>` with `FENV_ACCESS ON`; `_BitInt(128)`/`unsigned __int128` |
| Claimed error | ≈6.2×10⁻⁷ rad average (header comment) | correctly rounded (≤0.5 ulp) |
| License | GPL-compatible (GNU Radio) | MIT |

Both licenses are compatible with this project's GPLv3.

**Relevant synergy with an existing project rule:** `cr_atan2` depends on strict
IEEE-754 semantics and `#pragma STDC FENV_ACCESS ON`. `-ffast-math` would break
it — the same flag CLAUDE.md already bans for breaking the multipath filter's
NaN check. So the CORE-MATH prerequisite is already satisfied by policy.

## 3. How the replacement would be integrated

No source edit is proposed here (per the analysis-only scope), but the mechanics
are straightforward:

1. Vendor `atan2.c` + `tint.h` under, e.g., `extern/core-math/` and compile
   `atan2.c` as C into a small static lib (rename `cr_atan2` or expose it via a
   header `extern "C" double cr_atan2(double, double);`).
2. Replace the call site with the natural double-precision form — no narrowing:

   ```cpp
   double phase_err = cr_atan2(new_phasor_q, new_phasor_i);
   ```

3. Keep `-ffast-math` banned (already the case).

The `cr_atan2.o` produced on this host is ≈11.9 kB `__TEXT` — comparable to the
existing table's footprint, so binary size is not a factor.

## 4. Measurement method

Two standalone C++ harnesses, both built with the project flags
`-O3 -ftree-vectorize -std=c++20`:

* **`bench.cpp`** — accuracy and throughput. `fast_atan2f` was extracted verbatim
  from `include/Utility.h`; `cr_atan2` compiled from the downloaded CORE-MATH
  sources. Reference truth is `atan2l` (80-bit long double). Two 2²⁰-point data
  sets:
  * **A — PLL locked regime:** `x = new_phasor_i ~ N(0.104, 0.01)` (clamped > 0),
    `y = x·tan(err)` with `err ~ N(0, 0.0024)` — i.e. the actual distribution the
    locked loop sees.
  * **B — full four-quadrant range:** `x, y ~ U(-1, 1)`.
  Speed is best-of-5, 40 repetitions of 2²⁰ calls each, with a `volatile` sink to
  defeat dead-code elimination. `std::atan2` (double) and `atan2f` (float) are
  included as baselines.
* **`pll_sim.cpp`** — a byte-faithful replay of `PilotPhaseLock::process()`'s
  inner loop (same Direct-Form-2 biquad and first-order IIR coefficients, same
  min/max frequency clamp, same NCO recursion), fed a synthetic 0.1-amplitude
  19 kHz pilot + noise for 20 s at 384 kHz with a 0.3 rad phase step at t = 5 s.
  Run once with each atan2 backend; the tracked frequency and NCO phase are
  compared over the last 5 s (steady state).

## 5. Accuracy results

Absolute error vs `atan2l`, in radians:

| data set | function | max abs | rms | mean abs |
|---|---|---:|---:|---:|
| **A — PLL locked** | `fast_atan2f` (float) | 5.05×10⁻⁸ | 7.11×10⁻⁹ | 4.17×10⁻⁹ |
| | **`cr_atan2` (double)** | **1.74×10⁻¹⁸** | 3.88×10⁻²⁰ | 3.18×10⁻²¹ |
| | `std::atan2` (double) | 0 | 0 | 0 |
| | `atan2f` (float) | 1.19×10⁻⁹ | 1.02×10⁻¹⁰ | 6.54×10⁻¹¹ |
| **B — full range** | `fast_atan2f` (float) | 1.53×10⁻⁶ | 7.64×10⁻⁷ | 6.63×10⁻⁷ |
| | **`cr_atan2` (double)** | **4.44×10⁻¹⁶** | 2.48×10⁻¹⁷ | 2.16×10⁻¹⁸ |
| | `std::atan2` (double) | 0 | 0 | 0 |
| | `atan2f` (float) | 1.73×10⁻⁷ | 4.93×10⁻⁸ | 3.72×10⁻⁸ |

Notes:

* `fast_atan2f`'s full-range max error 1.53×10⁻⁶ rad confirms the header's
  "≈6.2×10⁻⁷ average" claim (max is a few × the average, as expected).
* `cr_atan2` is at the machine-epsilon floor (max 4.4×10⁻¹⁶ rad is ≈1 ulp of the
  ~1.5 rad output) — correctly rounded, as advertised.
* `std::atan2` on this host matches the long-double reference to the last bit for
  all 2×2²⁰ samples, so it too is effectively correctly rounded here.
* In the locked regime `fast_atan2f` is ≈**11× worse than even single-precision
  `atan2f`**, because the `double→float` narrowing plus 256-interval table is
  coarser than a real float `atan2`.

## 6. Speed results

Nanoseconds per call (best of 5, lower is better), Apple M2 Pro, `-O3
-ftree-vectorize`:

| data set | function | ns/call | vs `fast_atan2f` |
|---|---|---:|---:|
| **A — PLL locked** | `fast_atan2f` | 7.38 | 1.00× |
| | **`cr_atan2`** | **9.62** | **1.30× (slower)** |
| | `std::atan2` (double) | 4.56 | 0.62× (faster) |
| | `atan2f` (float) | 2.04 | 0.28× (faster) |
| **B — full range** | `fast_atan2f` | 13.48 | 1.00× |
| | `cr_atan2` | 8.82 | 0.65× (faster) |
| | `std::atan2` (double) | 23.65 | 1.75× (slower) |
| | `atan2f` (float) | 9.48 | 0.70× (faster) |

Reading these:

* **In the PLL's actual regime (A)**, `cr_atan2` costs **+2.24 ns/call (+30 %)**
  over `fast_atan2f`. Its fast (double-double) path is what runs here; the
  expensive `tint.h` path essentially never triggers for these well-conditioned
  arguments.
* **`fast_atan2f` is data-dependent and branchy**: its many
  quadrant/branch decisions predict perfectly in the locked regime (7.38 ns) but
  mispredict on random full-range data (13.48 ns). `cr_atan2` is far more
  branch-stable (9.6 → 8.8 ns).
* The headline surprise is the **`std::atan2` column**: in the locked regime the
  system double `atan2` is **1.6× faster than `fast_atan2f` and exact**. On an
  M2 Pro the table lookup is a pessimization, not an optimization.

## 7. End-to-end effect on the PLL

Replaying the exact loop (`pll_sim.cpp`) with each backend, comparing the last
5 s of a 20 s run:

```
tracked pilot freq  fast_atan2f : 19000.000101 Hz
tracked pilot freq  cr_atan2    : 19000.000101 Hz
|freq diff|                     : rms 3.26e-07 Hz,  max 1.51e-06 Hz
38 kHz subcarrier phase diff    : rms 2.21e-08 rad, max 1.01e-07 rad
separation if that were the only error : 291.9 dB
```

Put against the loop's own measured performance from
`doc/PLL_ANALYSIS_20260722.md` (pilot 19000.012 ± 0.044 Hz, phase-error rms
0.0024 rad, PLL-limited separation ≈105 dB rms / ≈72 dB worst):

* The frequency perturbation from the atan2 choice (max 1.5×10⁻⁶ Hz) is **~3×10⁴
  smaller** than the loop's own frequency jitter (0.044 Hz).
* The subcarrier phase perturbation (max 1.0×10⁻⁷ rad) would, *in isolation*,
  correspond to 292 dB of stereo separation — i.e. the phase detector's rounding
  is nowhere near the separation bottleneck (the loop's own ≈105 dB dominates by
  ~190 dB).

The extra accuracy is real but lands entirely below the loop's noise floor. The
PLL produces the same output either way.

## 8. Cost/benefit and recommendation

**Absolute cost of `cr_atan2`.** One call per 384 kHz sample × +2.24 ns =
**+0.86 ms of CPU per second of audio ≈ 0.086 % of one core.** Trivial. Cost is
not a reason to reject it.

**Benefit the PLL can use.** None measurable (§7). The loop's 0.0024 rad phase
noise swamps both the 5×10⁻⁸ rad table error and the 10⁻¹⁶ rad CR error.

**The one benefit that is unique to CORE-MATH:** *bit-for-bit identical output
on every platform and every libm.* System `atan2` is correctly rounded here on
macOS/arm64, but that is not guaranteed on all Linux libms/architectures the
project may build on. If reproducible pilot phase across builds ever matters
(e.g. regression-comparing decoder output byte-for-byte), `cr_atan2` is the only
option that guarantees it. That is the case for adopting it.

**Decision matrix:**

| Goal | Best choice | Why |
|---|---|---|
| Lowest CPU on this target, accuracy irrelevant | keep `fast_atan2f`, or use `atan2f` | 2–7 ns, table is "good enough" for the loop |
| More accuracy than the table, simplest change | **`std::atan2` (uncomment existing line)** | faster than the table here *and* exact; zero new code/deps |
| Bit-reproducible pilot phase across all platforms | **CORE-MATH `cr_atan2`** | only option that is correctly rounded *by construction* everywhere |
| Absolute lowest error, cost no object | `cr_atan2` | correctly rounded, +30 % vs table (negligible in absolute ms) |

**Recommendation.** Proposing `cr_atan2` as a *drop-in accuracy upgrade* is
technically sound and essentially free in CPU terms, but it buys the PLL nothing
audible and adds ~1100 lines of vendored code plus a `FENV_ACCESS`/`_BitInt(128)`
dependency. Adopt it **only** to gain cross-platform bit-reproducibility of the
phase detector. If the motivation is simply "stop using a lossy float table,"
the strictly better move on this hardware is to restore the already-present
`std::atan2(new_phasor_q, new_phasor_i)` call: it is faster than the table here,
correctly rounded, and needs no new code. Either way, remove the redundant
`double→float` narrowing that `fast_atan2f` forces.

## 9. Reproduction

Sources fetched from CORE-MATH `master`:

```
src/binary64/atan2/atan2.c   (cr_atan2, 586 lines)
src/binary64/atan2/tint.h    (192-bit accurate path, 543 lines)
```

Harnesses (in the analysis scratchpad): `bench.cpp` (accuracy + throughput),
`pll_sim.cpp` (loop replay), `fast_atan2f.h` (verbatim extract of
`include/Utility.h:158–302`).

```sh
clang   -O3 -ftree-vectorize -c atan2.c -o cr_atan2.o
clang++ -O3 -ftree-vectorize -std=c++20 bench.cpp    cr_atan2.o -o bench
clang++ -O3 -ftree-vectorize -std=c++20 pll_sim.cpp  cr_atan2.o -o pll_sim
```

Numbers above are from Apple M2 Pro / macOS 26.5.2 / Homebrew clang 22.1.8.
The ns/call figures are hardware-specific; the *ordering* (system `atan2`
faster than the table on a modern out-of-order arm64 FPU) is the portable
conclusion, and should be re-measured on the actual deployment target before
acting on the speed argument.
