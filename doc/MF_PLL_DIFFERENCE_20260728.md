# The pilot PLL across the multipath filter sweep, dev versus 20260716-0 (2026-07-28)

`doc/MF_PLL_DIFFERENCE_20260727.md` compared the two builds at a single
multipath filter setting, `-E 36`, and found the pilot PLL responsible for the
entire audio difference. That was the one setting at which the multipath filter
is guaranteed to behave identically in the two revisions. This report sweeps
`-E` over 18, 36, 50, 70 and 100 and asks what changes.

The answer is that `-E 36` is a singular point. The `dev` multipath filter
scales its adaptation step with the filter order; `20260716-0` does not, and the
two rules coincide at exactly 36 stages. Away from it the two builds run
genuinely different filters, the pilot the PLL receives is different, and the
audio difference grows by up to 15 dB. The PLL code change is unaffected by
`-E` and stays exactly what the previous report measured.

## Executive summary

- **The step-size rule is the whole story.** `dev` sets
  `alpha_effective = min(0.1 · N / 145, 0.5)` for filter order `N = 4·stages+1`,
  so its per-tap step `mu = alpha_effective / N` is pinned at 6.8966×10⁻⁴ for
  every setting in this sweep. `20260716-0` uses a fixed `alpha = 0.1`, so its
  `mu = 0.1/N` falls as 1/N. The two agree only at `N = 145`, i.e. `-E 36` (§2).
- **The audio difference is minimized at `-E 36` and grows away from it in both
  directions**, from −76.2 dBFS at 36 to −67.6 at 18 and −61.5 at 100 (§3).
- **Proved by construction.** `dev` with only the old constant-alpha rule
  restored reproduces `20260716-0` to **−76.5 dBFS at `-E 18` and −76.6 dBFS at
  `-E 100`** — the same floor as at `-E 36`, which is the PLL retune residual
  and nothing else (§3).
- **The step-size rule does what it was designed to do.** Residual pilot
  amplitude fluctuation — the fading the filter failed to remove — grows by
  **+2.42 dB in the 0.1–1 Hz band and +1.40 dB in 1–10 Hz** from `-E 18` to
  `-E 100` on `20260716-0`, and by only **+0.40 dB and +0.27 dB** on `dev`.
  The converged CMA residual grows by 5.2 dB on the old build and 1.6 dB on the
  new one (§5, §10).
- **It is not free.** Faster adaptation on a long filter costs gradient noise.
  At `-E 100` the new rule raises the loop's steady-state phase-error RMS by
  0.53 dB and its frequency jitter by 0.54 dB relative to the old rule on the
  same build; at `-E 18`, where the new rule is the *slower* of the two, it
  costs nothing (§9).
- **The PLL never re-acquired, in any of the twelve decodes.** Exactly one lock
  transition per run, zero post-lock counter resets, at every `-E` and in every
  build. But the loop was driven into its ±30 Hz frequency limiter — briefly
  open-loop — in 8 of the 12 (§8).
- **Which limiter events occur depends on both the build and `-E`.** The two
  deep channel fades saturate the old loop at every `-E` and never saturate the
  `dev` loop. The multipath filter's engagement transient saturates *both*
  loops, but only at `-E 50` and `-E 70` (§7, §8).
- **Correction to the previous report.** `MF_PLL_DIFFERENCE_20260727.md` §7.1
  gave the filter's inserted group delay as `stages*3+1` = 109 samples. It is
  `stages−1` = 35 samples. The sweep proves it: across `-E` the engagement kick
  is proportional to the *wrapped* phase step of a `stages−1` sample delay with
  R² = 0.99994, while the other reading gives R² = 0.69 (§6). That document has
  been corrected.
- **For scale**, what the filter itself does to the audio grows from
  −25.7 dBFS at `-E 18` to −18.4 dBFS at `-E 100` — at the top of the sweep the
  filter is altering the mono signal by only 1.4 dB less than the signal's own
  level. Every build-to-build difference in this report is 43 to 54 dB below
  that (§4).

## 1. Method

Three builds, all `-O3 -ftree-vectorize`:

| build | source | purpose |
|---|---|---|
| `old` | tag `20260716-0` (`dbca134`), pristine | reference |
| `new` | `dev` (`e3110ac`), pristine | subject |
| `hybalpha` | `dev` with `m_alpha(alpha)` — the old constant-alpha rule, everything else `dev` | isolate the step-size rule |

Twelve decodes: `old` and `new` at `-E` = 18, 36, 50, 70, 100, and `hybalpha` at
`-E` = 18 and 100, which are the sweep extremes. Each decode runs the full
200 s, so the sweep was run as two parallel batches.

Instrumentation, all stderr-only and touching no DSP arithmetic:

- **`DEBUG_PLL_FILTER`** — the stock flag in `sfmbase/PilotPhaseLock.cpp`, one
  line per block.
- **`DEBUG_PLL_TRACE`** — the 6 kHz loop-state trace introduced in
  `MF_PLL_DIFFERENCE_20260727.md` §1:
  `T,<sample index>,<m_freq Hz>,<m_freq_err Hz>,<phase_err rad>,<m_pilot_level>,<m_lock_cnt>`,
  1,200,000 rows per run.
- **`DEBUG_MF_PARAMS`** — one line at the first filtered block with the filter
  geometry and the effective step size actually in force:
  `MFPARAM,<stages>,<order>,<refidx>,<mu>,<alpha>`.
- **`DEBUG_MF_ERR`** — one line per filtered block with the constant-modulus
  residual and the peak output magnitude: `MFERR,<block>,<m_error>,<peak>`,
  37,400 rows per run (37,500 blocks less the 100 bypassed ones).

All four are injected through `EXTRA_FLAGS`; the hybrid lives in a throwaway
`git worktree`. The decode command, for every run:

```sh
airspy-fmradion -m fm -t filesource -E <stages> \
  -c freq=89700000,srate=384000,\
filename=test-files/AirSpy_20260727_125800Z_89700kHz_IQ.wav,wav,format=FLOAT \
  -G out.wav
```

All twelve outputs are 9,599,899 frames with M = −17.56 dBFS, so every
comparison is a straight sample-by-sample subtraction. The stereo onset is
0.1952 s for every `dev`-derived build and 0.4939 s for every `old` build,
independent of `-E` — the lock delay does not interact with the filter setting.

The `\r`-gluing hazard of `MF_PLL_DIFFERENCE_20260727.md` §1 applies to the new
line kinds too; the status line glues onto `MFERR` and `MFPARAM` lines exactly
as it does onto `T` lines.

## 2. The step-size rule

Both revisions run the same normalized CMA update
`m_mu = alpha_term / (Σ|window|² + 1e-10)`, recomputed at every coefficient
update. What changed is the numerator:

| | `20260716-0` | `dev` |
|---|---|---|
| numerator | `alpha` = 0.1, fixed | `m_alpha = min(alpha·N/145, 0.5)` |
| denominator | fresh float VOLK reduction each update | running `double` sum, resynced every 65536 updates |
| effective step | `mu ≈ 0.1/N` | `mu ≈ 6.8966×10⁻⁴`, order-independent |

The denominator change is a CPU optimization, not a numerical one: the old
float reduction carries about 2×10⁻⁶ relative error and the new incremental
double sum about 1.6×10⁻¹³ just before a resync. Neither shifts `mu` measurably.

With `N = 4·stages + 1`:

| `-E` | N | `mu` old | `alpha_eff` new | `mu` new | new/old |
|---|---|---|---|---|---|
| 18 | 73 | 1.3699×10⁻³ | 0.05034 | 6.8966×10⁻⁴ | **0.503** |
| 36 | 145 | 6.8966×10⁻⁴ | 0.10000 | 6.8966×10⁻⁴ | **1.000** |
| 50 | 201 | 4.9751×10⁻⁴ | 0.13862 | 6.8966×10⁻⁴ | 1.386 |
| 70 | 281 | 3.5587×10⁻⁴ | 0.19379 | 6.8966×10⁻⁴ | 1.938 |
| 100 | 401 | 2.4938×10⁻⁴ | 0.27655 | 6.8966×10⁻⁴ | 2.766 |

The `alpha_maximum = 0.5` clamp binds only from `stages ≥ 181`, so nothing in
this sweep reaches it. The `MFPARAM` lines confirm every row of the table from
the running binaries, to the printed precision:

```
MFPARAM,18,73,55,0.00136986305006,0.100000002654       old  -E 18
MFPARAM,18,73,55,0.000689655193128,0.0503448290983     new  -E 18
MFPARAM,18,73,55,0.00136986305006,0.100000002654       hybalpha -E 18
MFPARAM,36,145,109,0.000689655193128,0.100000003004    both -E 36
MFPARAM,100,401,301,0.000249376549618,0.0999999963969  old  -E 100
MFPARAM,100,401,301,0.000689655193128,0.276551732444   new  -E 100
```

At `-E 36` the two rules produce bit-identical constants. That is why the
previous report could attribute everything to the PLL: at that one setting the
multipath filters were the same filter.

## 3. The audio difference versus `-E`

`old` versus `new`, whole file and with the start-up transient discarded:

| `-E` | whole file | after 0.5 s | M relative to M | S relative to S |
|---|---|---|---|---|
| 18 | −52.13 | −67.62 | −55.17 | −42.65 |
| **36** | −52.24 | **−76.22** | **−133.82** | −49.65 |
| 50 | −52.20 | −71.15 | −60.53 | −45.55 |
| 70 | −52.06 | −65.67 | −54.15 | −40.32 |
| 100 | −51.77 | **−61.50** | −49.70 | −36.23 |

(dBFS and dB.) The whole-file column barely moves because it is dominated by
the 0.2–0.5 s stereo-onset transient, which is a PLL effect and `-E`-independent
— which is exactly the trap the previous report warned about. The after-0.5 s
column is the real signal, and it has a clear minimum at `-E 36`.

The M column is the sharpest indicator. At `-E 36` the mono sum matches at the
float noise floor, −133.82 dB, because the two multipath filters are identical
and the only difference is a stereo-side PLL effect. At every other setting M
jumps by 73 to 84 dB, to between −49.7 and −60.5 dB. That is not a PLL
difference — the PLL cannot move M — it is two different filters producing two
different IF signals.

The hybrid settles the attribution:

| `-E` | `hybalpha` vs `old` | `hybalpha` vs `new` |
|---|---|---|
| 18 | **−76.54** | −68.22 |
| 100 | **−76.61** | −61.63 |

`dev` with only the constant-alpha rule restored reproduces the old build to
−76.5 dBFS at both sweep extremes — the same −76.2 dBFS floor the two builds
show at `-E 36`, which `MF_PLL_DIFFERENCE_20260727.md` §9 identified as the PLL
loop retune. Meanwhile `hybalpha` versus `new` reproduces the full `-E`-dependent
difference. The step-size rule accounts for all of it; nothing is left over for
the ring-buffer rewrite or the state-power precision change.

## 4. For scale: what the filter itself does

Each `dev` decode against the same recording decoded with no `-E` at all,
after 0.5 s:

| `-E` | RMS(with − without) | M relative to M | S relative to S |
|---|---|---|---|
| 18 | −25.73 dBFS | −9.10 dB | −6.35 dB |
| 36 | −22.57 dBFS | −5.72 dB | −4.22 dB |
| 50 | −21.26 dBFS | −4.38 dB | −3.09 dB |
| 70 | −19.80 dBFS | −2.88 dB | −1.90 dB |
| 100 | −18.41 dBFS | −1.44 dB | −0.78 dB |

At `-E 100` the filter changes the mono signal by an amount only 1.4 dB below
the mono signal itself. The largest build-to-build difference in this report,
−61.5 dBFS at the same setting, is 43 dB below that; at `-E 36` the gap is
54 dB. Choosing `-E` matters enormously more than choosing the build.

## 5. How the fading changes

The loop's `m_pilot_level` is a direct measurement of the recovered pilot's
amplitude, so its fluctuation is the residual fading the multipath filter
failed to remove. Measured over t ∈ [1, 200] s with the two deep fade windows
excluded, as the standard deviation of 20·log₁₀(pilot / median) and as
integrated RMS in bands:

| build | `-E` | spread (dB) | 0.1–1 Hz | 1–10 Hz | 10–100 Hz | 100–1000 Hz |
|---|---|---|---|---|---|---|
| `old` | 18 | 0.2191 | 0.0806 | 0.1697 | 0.0642 | 0.0078 |
| `old` | 36 | 0.2326 | 0.0902 | 0.1791 | 0.0634 | 0.0080 |
| `old` | 50 | 0.2397 | 0.0935 | 0.1843 | 0.0633 | 0.0082 |
| `old` | 70 | 0.2494 | 0.0992 | 0.1910 | 0.0636 | 0.0085 |
| `old` | 100 | **0.2617** | **0.1065** | **0.1993** | 0.0640 | 0.0089 |
| `new` | 18 | 0.2297 | 0.0869 | 0.1774 | 0.0650 | 0.0092 |
| `new` | 36 | 0.2338 | 0.0902 | 0.1796 | 0.0658 | 0.0099 |
| `new` | 50 | 0.2342 | 0.0894 | 0.1802 | 0.0662 | 0.0102 |
| `new` | 70 | 0.2358 | 0.0900 | 0.1813 | 0.0669 | 0.0105 |
| `new` | 100 | **0.2380** | **0.0910** | **0.1830** | 0.0678 | 0.0110 |
| `hybalpha` | 18 | 0.2204 | 0.0806 | 0.1702 | 0.0667 | 0.0096 |
| `hybalpha` | 100 | 0.2628 | 0.1065 | 0.1999 | 0.0664 | 0.0111 |

The fading on this recording lives in the 0.1–10 Hz bands. Raising `-E` from 18
to 100 costs the old build **+2.42 dB at 0.1–1 Hz and +1.40 dB at 1–10 Hz**; it
costs `dev` **+0.40 dB and +0.27 dB**. Lengthening the filter on the old build
makes it track the fading progressively worse, which is precisely the failure
mode the step-size rule was written to prevent, and it is prevented.

`hybalpha` reproduces `old` in those bands to 0.02–0.03 dB at both extremes, so
the fading-tracking behavior belongs entirely to the step-size rule.

`new` relative to `old`, per band:

| `-E` | 0.1–1 Hz | 1–10 Hz | 10–100 Hz | 100–1000 Hz |
|---|---|---|---|---|
| 18 | **+0.67** | **+0.39** | +0.10 | +1.51 |
| 36 | 0.00 | +0.02 | +0.31 | +1.87 |
| 50 | −0.40 | −0.20 | +0.38 | +1.82 |
| 70 | −0.87 | −0.46 | +0.43 | +1.78 |
| 100 | **−1.40** | **−0.75** | +0.49 | +1.77 |

(dB; negative means `dev` leaves less residual fading.) The crossover is at
`-E 36` by construction. Below it `dev` is *worse*, because its rule makes the
step twice as slow at `-E 18`; above it `dev` is better, by up to 1.4 dB.

The 100–1000 Hz column is a constant +1.8 dB penalty that does not depend on
`-E`, and `hybalpha` shows the same +1.85/+1.88 dB against `old` regardless of
setting. That band is not the filter at all — it is the PLL loop retune, whose
wider phasor low-pass passes about 1.2 dB more noise above 100 Hz
(`MF_PLL_DIFFERENCE_20260727.md` §6).

## 6. The engagement phase step, and a correction

`FmDecoder` bypasses the multipath filter for 100 blocks and enables it at
t = 0.533333 s. At that instant the filter's coefficients are still the initial
ones — reference tap `1+0j`, all others zero — so the signal path abruptly
acquires the reference tap's group delay. The previous report gave that delay
as `stages*3+1` samples. That was wrong, and the sweep is what exposes it.

In `single_process()` the window is ordered newest-last: `m_window[N−1]` holds
the sample just written and `m_window[0]` the one written `N−1` samples ago, so
tap *i* has delay `N−1−i`. The reference tap at index `3·stages+1` therefore
carries a delay of

    (4·stages+1) − 1 − (3·stages+1) = stages − 1 samples.

A step delay of `d` samples shifts the 19 kHz pilot by `d·19000/384000` cycles,
and only the wrapped remainder matters to a phase-locked loop:

| `-E` | delay (samples) | delay (µs) | phase at 19 kHz | wrapped | measured kick, `new` | `old` |
|---|---|---|---|---|---|---|
| 18 | 17 | 44.27 | 0.841 cyc | **−57.2°** | +13.07 Hz | +14.19 Hz |
| 36 | 35 | 91.15 | 1.732 cyc | **−96.6°** | +22.36 Hz | +24.28 Hz |
| 50 | 49 | 127.60 | 2.424 cyc | **+152.8°** | −29.53 Hz (railed) | −29.53 Hz (railed) |
| 70 | 69 | 179.69 | 3.414 cyc | **+149.1°** | −29.53 Hz (railed) | −29.53 Hz (railed) |
| 100 | 99 | 257.81 | 4.898 cyc | **−36.6°** | +8.36 Hz | +9.18 Hz |

The kick is not monotonic in `-E`, which is the whole point: it follows the
wrapped phase, not the delay. Fitting the three unsaturated points:

| delay hypothesis | slope | intercept | max residual | R² |
|---|---|---|---|---|
| `stages − 1` | −0.2336 Hz/deg | −0.22 Hz | 0.063 Hz | **0.999940** |
| `3·stages + 1` | +0.0472 Hz/deg | +14.55 Hz | 4.38 Hz | 0.694 |

Three points spanning a 5.6× range of `-E`, fitted to 0.06 Hz on a 22 Hz
excursion, with the sign of the kick flipping exactly where the wrapped phase
changes sign. `stages − 1` is right. `-E 50` and `-E 70`, whose wrapped steps
of +153° and +149° would demand 35.7 and 34.8 Hz, both saturate the ±30 Hz
limiter instead — which is itself a confirmation, since the linear fit predicts
saturation for those two settings and no others.

`doc/MF_PLL_DIFFERENCE_20260727.md` has been corrected in place.

The practical consequence: the size of the self-inflicted disturbance at filter
engagement is an essentially arbitrary function of `-E`, ranging from 8 Hz to
beyond the loop's frequency limit. A crossfade at engagement, or engaging while
the pilot is not yet trusted, would remove it entirely.

## 7. The three disturbance events versus `-E`

The recording's two genuine channel fades are at t = 52.148 s and 130.380 s.
The third event at t = 0.537 s is the engagement transient of §6.

**The two channel fades are a property of the recording, not of the filter.**
Their depth barely moves across the sweep:

| event | build | `-E 18` | `-E 36` | `-E 50` | `-E 70` | `-E 100` |
|---|---|---|---|---|---|---|
| 52.148 s | `old` | −5.90 | −5.90 | −5.89 | −5.89 | −5.88 |
| | `new` | −5.98 | −5.98 | −5.98 | −5.99 | −5.97 |
| 130.380 s | `old` | −6.10 | −5.96 | −5.88 | −5.84 | −5.80 |
| | `new` | −6.07 | −6.05 | −6.00 | −6.02 | −6.00 |

(pilot depth in dB relative to that decode's median.) A longer filter shaves
0.1–0.3 dB off the old build's fade depth at 130 s and essentially nothing at
52 s. Neither build turns a −6 dB fade into anything the loop treats
differently.

The loop's response to the fades is likewise `-E`-independent and depends only
on the build, exactly as the previous report found at `-E 36`:

| build | peak Δf at 52.15 s | peak phase error | back inside ±1 Hz |
|---|---|---|---|
| `old`, all `-E` | −29.53 Hz (**railed**) | −1.516 rad | 47.0–47.3 ms |
| `new`, all `-E` | −28.5 Hz | −1.585 rad | 53.5–53.7 ms |

The spread across the five `-E` values is 0.02 Hz and 0.3 ms. The old loop
saturates its limiter at both fades at every setting; the `dev` loop never does.

## 8. Did the PLL have to re-acquire?

No, in any decode. But it did go open-loop.

| build | `-E` | t of lock | lock transitions | post-lock counter resets | limiter events |
|---|---|---|---|---|---|
| `old` | 18 | 0.5013 | 1 | 0 | 52.152 s (1.50 ms), 130.384 s (1.50 ms) |
| `old` | 36 | 0.5013 | 1 | 0 | 52.152 s (1.33 ms), 130.384 s (1.50 ms) |
| `old` | 50 | 0.5013 | 1 | 0 | **0.538 s (2.50 ms)**, 0.541 s, 52.152 s (1.17 ms), 130.384 s (1.50 ms) |
| `old` | 70 | 0.5013 | 1 | 0 | **0.538 s (2.83 ms)**, 52.152 s (1.33 ms), 130.384 s (1.33 ms) |
| `old` | 100 | 0.5013 | 1 | 0 | 52.152 s (1.33 ms), 130.384 s (1.50 ms) |
| `new` | 18 | 0.2027 | 1 | 0 | none |
| `new` | 36 | 0.2027 | 1 | 0 | none |
| `new` | 50 | 0.2027 | 1 | 0 | **0.538 s (2.33 ms)** |
| `new` | 70 | 0.2027 | 1 | 0 | **0.538 s (2.50 ms)** |
| `new` | 100 | 0.2027 | 1 | 0 | none |
| `hybalpha` | 18 | 0.2027 | 1 | 0 | none |
| `hybalpha` | 100 | 0.2027 | 1 | 0 | none |

Every run locks once and stays locked: one transition, no `m_lock_cnt` reset
after lock, at every setting. The lock test `2·m_pilot_level > minsignal` trips
below `m_pilot_level = 0.0005`; the deepest excursion anywhere in the sweep is
0.0096, at `new -E 50` during the engagement transient — a factor of 19 clear
of the threshold.

The limiter column separates the two causes cleanly:

- The **channel fades** saturate the `old` loop at every `-E` and the `dev` loop
  at none. That is the loop retune, and it is `-E`-independent.
- The **engagement transient** saturates *both* loops, and only at `-E 50` and
  `-E 70`. That is the wrapped phase step of §6, and it is build-independent.

So "did the PLL re-tune?" has two answers depending on what is meant. It never
re-acquired lock. It was pushed against its frequency limit, and therefore ran
briefly open-loop, in eight of the twelve decodes, for 1.2 to 2.8 ms at a time.

## 9. Steady-state loop quality versus `-E`

Over the quiet window (t ∈ [1, 200] s, deep fades excluded):

| build | `-E` | `m_freq` std (Hz) | phase error RMS (rad) | mean pilot |
|---|---|---|---|---|
| `old` | 18 | 0.03647 | 0.001913 | 0.047648 |
| `old` | 36 | 0.03809 | 0.001998 | 0.047584 |
| `old` | 50 | 0.04043 | 0.002121 | 0.047550 |
| `old` | 70 | 0.04413 | 0.002313 | 0.047507 |
| `old` | 100 | 0.04890 | 0.002562 | 0.047455 |
| `new` | 18 | 0.03466 | 0.002045 | 0.047578 |
| `new` | 36 | 0.03600 | 0.002123 | 0.047553 |
| `new` | 50 | 0.03829 | 0.002257 | 0.047547 |
| `new` | 70 | 0.04231 | 0.002492 | 0.047535 |
| `new` | 100 | 0.04906 | 0.002885 | 0.047517 |
| `hybalpha` | 18 | 0.03445 | 0.002032 | 0.047618 |
| `hybalpha` | 100 | 0.04611 | 0.002714 | 0.047424 |

**Raising `-E` degrades the loop in both builds** — phase-error RMS rises by
2.5 dB (old) and 3.0 dB (new) from `-E 18` to `-E 100`. A longer filter is not
free for the PLL regardless of which step-size rule is used.

The frequency-jitter spectrum shows the same thing with the crossover visible.
`new` relative to `old`, integrated PSD of `m_freq` per band:

| `-E` | 0.1–1 Hz | 1–10 Hz | 10–100 Hz | 100–1000 Hz |
|---|---|---|---|---|
| 18 | −0.77 | +0.14 | **−0.76** | +1.12 |
| 36 | +0.02 | −0.03 | −0.72 | +0.94 |
| 50 | +0.53 | +0.03 | −0.57 | +1.15 |
| 70 | +1.17 | +0.39 | −0.25 | +1.62 |
| 100 | **+1.90** | **+1.16** | +0.43 | **+2.39** |

(dB; positive means `dev` is noisier.) At `-E 18` `dev` is quieter in the loop's
own resonance region, which is the retune of `MF_PLL_DIFFERENCE_20260727.md` §6
showing through. By `-E 100` it is worse in every band. Differences below about
0.5 dB should be read as run-to-run noise.

`hybalpha` decomposes the two changes:

| quantity | `-E 18` | `-E 100` |
|---|---|---|
| PLL retune (`hybalpha` − `old`), phase RMS | **+0.52 dB** | **+0.50 dB** |
| step-size rule (`new` − `hybalpha`), phase RMS | +0.06 dB | **+0.53 dB** |
| PLL retune, `m_freq` std | −0.49 dB | −0.51 dB |
| step-size rule, `m_freq` std | +0.05 dB | +0.54 dB |

The PLL retune contributes a constant ±0.5 dB — lowering frequency jitter,
raising phase-error RMS — independent of `-E`, exactly as
`MF_PLL_DIFFERENCE_20260727.md` §6 measured at one setting.

The step-size rule contributes nothing at `-E 18`, where it makes adaptation
*slower*, and **+0.53 dB at `-E 100`**, where it makes adaptation 2.77× faster.
That is gradient noise: a faster step on a 401-tap filter, most of whose taps
carry no real echo energy, injects more misadjustment noise onto the pilot. It
is the price of the fading-tracking improvement in §5, and the two are the same
knob turned in opposite directions.

## 10. Multipath filter convergence and residual

From the `MFERR` series, over the second half of each run:

| build | `-E` | converged mean \|error\| | std | max \|error\| | predicted τ (ms) |
|---|---|---|---|---|---|
| `old` | 18 | **0.013462** | 0.022244 | 0.526 | 7.60 |
| `old` | 36 | 0.016745 | 0.028145 | 0.489 | 15.10 |
| `old` | 50 | 0.018698 | 0.030949 | 0.717 | 20.94 |
| `old` | 70 | 0.021192 | 0.034680 | 0.731 | 29.27 |
| `old` | 100 | **0.024597** | 0.039899 | 0.887 | 41.77 |
| `new` | 18 | **0.016196** | 0.026807 | 0.639 | 15.10 |
| `new` | 36 | 0.016745 | 0.028145 | 0.489 | 15.10 |
| `new` | 50 | 0.017166 | 0.028598 | 0.577 | 15.10 |
| `new` | 70 | 0.017961 | 0.029769 | 0.643 | 15.10 |
| `new` | 100 | **0.019430** | 0.032201 | 0.770 | 15.10 |
| `hybalpha` | 18 | **0.013462** | 0.022244 | 0.526 | 7.60 |
| `hybalpha` | 100 | **0.024597** | 0.039899 | 0.887 | 41.77 |

(τ is the idealized single-mode time constant `N/alpha_eff` updates at
96 kHz — one update per four IF samples. It is a white-input approximation and
is quoted as a scaling law, not a measured settling time.)

**The predicted τ is not confirmed by measurement, and should not be quoted as
a convergence time.** A literal "time for |m_error| to fall below and stay below
K× its converged level" is not well posed on this recording: the block-level
residual is noise-dominated from the first block (its standard deviation is
1.3–2× its own mean) and the fading channel drives it above 1.1× the converged
level 28–41 % of the time throughout the whole 200 s, not only at the start. A
transient is unambiguously present — a z-test of the first `2·τ_pred` blocks
against the steady-state distribution gives z = 5.9–16.6 in every decode — but a
short-window exponential fit puts the measured decay at 35–80 ms in all twelve
decodes, with tens of percent uncertainty, i.e. **roughly flat across a 5.5×
range of `mu`**. The measured-to-predicted ratio falls from ~7× at the fastest
step to ~1.5× at the slowest. Whatever sets the observed settling on this
recording, it is not the single-mode `1/mu` law; the step size clearly governs
the *steady-state residual* (below) but the transient is dominated by something
else, most likely the channel's own time variation.

Two results:

- The converged residual grows by **5.2 dB** across the sweep on `old` and
  **1.6 dB** on `new`. Holding `mu` constant flattens the residual-versus-order
  curve substantially but does not make it flat.
- `hybalpha` reproduces `old`'s converged residual to all six printed digits at
  both extremes, and the underlying series agree block by block far more
  tightly than that:

  | `-E` | RMS(`hybalpha` − `old`) | RMS(`new` − `old`) | ratio |
  |---|---|---|---|
  | 18 | 3.18×10⁻⁷ | 9.45×10⁻³ | 3.4×10⁻⁵ (**−89.5 dB**) |
  | 100 | 4.10×10⁻⁷ | 2.15×10⁻² | 1.9×10⁻⁵ (**−94.4 dB**) |

  Correlation between `hybalpha` and `old` is 1.000000000 at both settings,
  against 0.943 and 0.851 for `new` against `old`. The step-size numerator
  accounts for essentially all of the filter's behavioral difference; the ring
  buffer, the double-precision running power sum and the resync interval
  together account for 3×10⁻⁵ of it.

Divergence headroom is comfortable everywhere: the largest `|m_error|` in the
whole sweep is 0.887 at `old -E 100` and the largest peak output magnitude is
1.409, against a `divergence_limit` of 10 — 21.0 dB and 17.0 dB of headroom at
the worst setting, 26.2 dB and 17.1 dB at `-E 36`. No run ever tripped a reset.
About 1.6–1.9 % of blocks exceed 5× the converged residual in every decode, but
they are spread uniformly over the whole 200 s rather than clustered near the
start — a fading-channel signature, not a convergence artifact.

## 11. What this establishes, and what it does not

- **It closes the caveat left open by `MF_DIFFERENCE_20260727.md` §7.** That
  report said the alpha order-scaling was cleared only at `-E 36`, where it is a
  bit-exact no-op, and that nothing had been measured elsewhere. It has now been
  measured at 18, 50, 70 and 100, and the rule behaves as designed: better
  fading tracking above the reference order, worse below it, with all of the
  build-to-build difference attributable to it and none to the surrounding
  rewrite.
- **It establishes the trade the rule makes.** Better fade tracking above
  `-E 36` (up to −1.4 dB of residual fading at `-E 100`) is bought with
  gradient noise (+0.53 dB of PLL phase jitter at the same setting). Which side
  of that trade is worth more is a listening question on a fading channel, not a
  measurement question, and `doc/MULTIPATH_FILTER_DESIGN_20260724.md` §17.4
  already cautions that the filter is a net loss on fading channels either way.
- **It does not endorse constant `mu` as the correct scaling law.**
  `MULTIPATH_FILTER_DESIGN_20260724.md` §15.4 measured out to `-E 400` and
  concluded the truth is closer to constant `alpha` than to constant `mu`. This
  sweep stops at 100 and says nothing about the far end; within 18–100 the
  shipped rule is a clear improvement above the reference order.
- **It does not evaluate audio quality.** No metric here says which build or
  which `-E` sounds better.
- **The `-E 18` regression is real and should be noted.** At `-E 18` the shipped
  rule halves the adaptation rate relative to `20260716-0` and leaves +0.67 dB
  more residual fading in the 0.1–1 Hz band. Anyone running short filters on a
  fading channel is getting slower tracking than the old build gave them.

## 12. Reproduction

```sh
S=/tmp/esweep
for v in old new hybalpha; do
  case $v in old) ref=20260716-0 ;; *) ref=dev ;; esac
  git worktree add --detach $S/$v $ref
  cp -R r8brain-free-src $S/$v/ && rm -f $S/$v/r8brain-free-src/.git
done
# hybalpha: in $S/hybalpha/sfmbase/MultipathFilter.cpp replace the initializer
#   m_alpha(std::min(alpha * static_cast<double>(m_filter_order) /
#                        static_cast<double>(alpha_reference_order),
#                    alpha_maximum))
# with
#   m_alpha(alpha)
for v in old new hybalpha; do
  cmake -S $S/$v -B $S/$v/build \
    -DEXTRA_FLAGS="-DDEBUG_PLL_FILTER -DDEBUG_PLL_TRACE -DDEBUG_MF_PARAMS -DDEBUG_MF_ERR"
  cmake --build $S/$v/build --target airspy-fmradion
done
```

`DEBUG_PLL_TRACE` is given in `MF_PLL_DIFFERENCE_20260727.md` §11. The two
multipath flags go in `MultipathFilter::process()` — the parameter line once at
the top, guarded by a function-local `static bool`, and the error line just
before the successful `return true`:

```cpp
#ifdef DEBUG_MF_PARAMS
  { static bool done = false;
    if (!done) { done = true;
      fmt::println(stderr, "MFPARAM,{},{},{},{:.12g},{:.12g}", m_stages,
                   m_filter_order, m_index_reference_point,
                   static_cast<double>(m_mu),
                   static_cast<double>(m_mu) * m_filter_order); } }
#endif
```

Note that `dev` already includes `<fmt/format.h>` behind
`#ifdef DEBUG_MULTIPATH_FILTER`, so a new flag needs its own include guard or
the build fails with "use of undeclared identifier 'fmt'".

Run the decode of §1 at each `-E`, split the glued status lines
(`perl -pe 's/(:Pilot=\s*-?[\d.]+)(T,|MFERR,|MFPARAM,)/$1\n$2/'`), then the two
numbers that settle the question:

```python
import numpy as np, soundfile as sf
db = lambda v: 20 * np.log10(np.sqrt(np.mean(v ** 2)))
for e in (18, 36, 50, 70, 100):
    a, sr = sf.read(f"old-E{e}.wav", dtype="float64", always_2d=True)
    b, _  = sf.read(f"new-E{e}.wav", dtype="float64", always_2d=True)
    n = min(len(a), len(b)); i = int(0.5 * sr)
    M = (a[i:n, 0] + a[i:n, 1]) / 2 - (b[i:n, 0] + b[i:n, 1]) / 2
    print(e, f"{db(a[i:n] - b[i:n]):.2f} dBFS   M {db(M) - db((a[i:n,0]+a[i:n,1])/2):.2f} dB")
```

The M column must read about −134 dB at `-E 36` and −50 to −61 dB everywhere
else. Repeat with the `hybalpha` binary in place of `new` and the whole
`-E` dependence must collapse back to about −76.5 dBFS.
