# What the pilot PLL does differently on dev and 20260716-0 (2026-07-27)

`doc/MF_DIFFERENCE_20260727.md` established that the whole audio difference
between the build at tag `20260716-0` (`dbca134`) and the current `dev`
(`6ea7031`) on `test-files/AirSpy_20260727_125800Z_89700kHz_IQ.wav` comes from
the pilot PLL, not from the multipath filter. This report opens the PLL itself.
It instruments the loop's internal state at 6 kHz for the whole 200-second
decode, on six builds that isolate each of the four source changes, and says
exactly what each one does.

The short answer: of the four changes, **two are inert and two are real**. The
lock-decision constant changes *when* the decoder trusts the loop but not one
bit of what the loop computes. The loop-filter retune changes the loop's
dynamics, and every audible consequence of it on this recording sits inside
three short windows where the pilot fades — two of which are genuine channel
events, and one of which is caused by the multipath filter switching itself on.

## Executive summary

- **The lock delay is bookkeeping only.** `dev` built with the old
  `15.0 / bandwidth_pll` produces a PLL trace that is **bit-identical** to `dev`
  (difference exactly 0.000000 over all 1,200,000 trace samples) and audio that
  is bit-identical after t = 0.5 s. It moves the stereo onset from
  **0.4939 s to 0.1952 s** and does nothing else (§5).
- **The loop acquires in ~50 ms, so both lock delays are conservative.** The
  loop is inside ±2 Hz of its final frequency at 37.7 ms (`dev`) / 47.0 ms
  (`20260716-0`) and inside ±1 Hz at 53.5 ms / 72.3 ms. The 200 ms threshold
  has about 4× margin on this recording; the old 500 ms threshold had 10× (§4).
- **The retune does what it was designed to do.** Widening the in-loop phasor
  low-pass and rescaling the PI gains by ×0.889 moves ζ from 0.573 to 0.710 and
  the phase margin from 46.2° to 51.6°, holding f_n at ~22 Hz. Measured
  closed-loop jitter confirms it: the `dev` loop passes **0.5–1.1 dB less**
  noise in the 10–50 Hz resonance region and 0.5–1.2 dB more above 100 Hz (§6).
- **Steady state is the same loop.** Over 192.8 s of undisturbed signal the two
  builds' frequency estimates differ by **0.0054 Hz RMS** on a 19 kHz carrier
  (−131 dB) and their phase errors by 3.0×10⁻⁴ rad (§6).
- **This recording contains three pilot fades**, at t = 0.537 s, 52.148 s and
  130.380 s, each 5–6 ms deep and −4 to −6 dB. **99.84 % of the audio
  difference attributable to the retune lies inside those three windows**;
  outside them the two loops agree to −124.3 dBFS (§7).
- **At two of the three fades the old loop rails against its own ±30 Hz
  frequency limiter** (`m_freq` pinned at exactly 18970.000000 Hz for 1.33 ms
  and 1.50 ms); the `dev` loop stays about 1 Hz clear of the rail and never
  saturates. This is the one place where the retune is unambiguously better
  behaved (§7).
- **The `fast_atan2f` → `std::atan2` swap is measurably nothing.** Regressing
  one build's phase error on the other gives slope 1.000000, intercept
  −1.3×10⁻¹² rad, residual 6.4×10⁻⁹ rad; the resulting audio difference is
  −171.0 dBFS (§8).
- **Correction to the previous report.** `MF_DIFFERENCE_20260727.md` §6 stated
  that the two residual bursts do not coincide with a pilot fade. They do. That
  conclusion rested on the status line, which prints a 10-block moving average
  of `2 * m_pilot_level` only every 20 blocks (107 ms) and averages a 5 ms fade
  away entirely. The dense trace resolves it (§7).
- **Incidental finding, common to both builds:** the multipath filter is
  bypassed for the first 100 blocks and engages abruptly at t = 0.533333 s,
  inserting its reference-tap group delay of 109 IF samples (284 µs, 5.4 pilot
  cycles) as a step. That step kicks the PLL by **+22 Hz** and takes ~90 ms to
  absorb. Decoding without `-E` leaves the loop within 1.5 Hz across the same
  window (§7.1).

## 1. Method

Six builds, all `-O3 -ftree-vectorize`, differing only inside
`PilotPhaseLock`:

| build | source | isolates |
|---|---|---|
| `old` | tag `20260716-0` (`dbca134`), pristine | reference |
| `new` | `dev` (`6ea7031`), pristine | subject |
| `hybloop` | `dev` + old biquad coefficients **and** old PI gains | the loop retune |
| `hybbq` | `dev` + old biquad coefficients only | the LPF half of the retune |
| `hybatan` | `dev` + `Utility::fast_atan2f` (and the old `Utility.h`) | the phase detector |
| `hyblock` | `dev` + old `m_lock_delay` (`15.0 / bandwidth_pll`) | the lock delay |

Each build carries two stderr-only compile flags that touch no DSP arithmetic:

- **`DEBUG_PLL_FILTER`** — the flag the task asked for, already present in
  `sfmbase/PilotPhaseLock.cpp:134`. It prints `m_freq`, `m_freq_err` and
  `m_pilot_level` once per block, i.e. 37,500 lines over the run (187 Hz).
- **`DEBUG_PLL_TRACE`** — added for this measurement, printing from the same
  point in the sample loop but every 64 IF samples, giving **6 kHz** resolution
  and adding the phase-detector output and the lock counter:
  `T,<sample index>,<m_freq Hz>,<m_freq_err Hz>,<phase_err rad>,<m_pilot_level>,<m_lock_cnt>`.

The block-rate flag alone is not enough for this question. The loop's natural
frequency is ~22 Hz and the events that matter last 5 ms; at 187 Hz a fade is
one or two samples. Everything in §4 and §7 needs the 6 kHz trace. Both flags
are injected through `EXTRA_FLAGS` so the working tree stays clean, and the
hybrids live in throwaway `git worktree` checkouts.

The trace is decimated, not filtered, but `phase_err` and `m_pilot_level` are
taken downstream of the in-loop biquad whose corners are 40 Hz and 188 Hz, and
`m_freq` is downstream of an integrator, so nothing above a few hundred Hz
survives to alias into the 3 kHz Nyquist band.

Decode command, identical for every build:

```sh
airspy-fmradion -m fm -t filesource -E 36 \
  -c freq=89700000,srate=384000,\
filename=test-files/AirSpy_20260727_125800Z_89700kHz_IQ.wav,wav,format=FLOAT \
  -G out.wav
```

`filesource` paces itself to real time, so each decode takes the full 200 s.
Output is 48 kHz float32 stereo, 9,599,899 frames; all six builds produce
identical frame counts and channel levels (L −17.02 dBFS, R −17.07 dBFS), so
every audio comparison is a straight sample-by-sample subtraction.

**Instrumentation pitfall worth recording.** The periodic status line is
printed to *stderr* with a leading `\r` and no trailing newline. It therefore
glues itself onto the front of the next `fmt::println(stderr, ...)`, exactly
1875 times per run. A parser keyed on `line.startswith("T,")` silently drops
those 1875 rows and reports index gaps that look like a block-structure
artifact. Split the status line off before parsing (`s/(:Pilot=\s*[-\d.]+)(T,)/$1\n$2/`)
and each log yields exactly 1,200,000 trace rows with a uniform stride of 64
and no gaps.

## 2. What actually changed in the source

`PilotPhaseLock::process()` is byte-identical between the two revisions except
for a single line. Everything else is a construction-time constant or a type.

| # | change | behavioral? |
|---|---|---|
| 1 | in-loop phasor biquad coefficients | **yes** — §3, §6, §7 |
| 2 | first-order (PI) section gains, ×0.8889688263 | **yes** — §3, §7 |
| 3 | `m_lock_delay`: `15.0 / bandwidth` → `6.0 / bandwidth_pll` | **yes**, but only for lock timing — §5 |
| 4 | `Utility::fast_atan2f` → `std::atan2` | measurable but negligible — §8 |
| 5 | `int` → `unsigned int` on `m_lock_delay`, `m_lock_cnt`, `m_pilot_periods`, `pilot_frequency` | **no** |
| 6 | dead members `m_biquad_phasor_i2` / `_q2` removed | **no** |
| 7 | `bandwidth` → `bandwidth_pll` rename, includes, comments | **no** |

Items 5–7 are inert, not merely believed to be:

- `m_lock_cnt += n` already took an `unsigned int n` in both revisions; the
  counter is bounded near `m_lock_delay + block_size`, i.e. below 3×10⁵, so
  neither the old int↔unsigned round trip nor the new native unsigned add can
  overflow or truncate.
- `was_locked = (m_lock_cnt >= m_lock_delay)` and `locked()` compare two
  operands that changed type *together*, and neither is ever negative.
- `2 * m_pilot_level > minsignal` involves only doubles.
- `m_pilot_periods == pilot_frequency` ranges over [0, 19000].
- Both `15.0 / bandwidth` and `6.0 / bandwidth_pll` are *exact* integers
  (192000.0 and 76800.0) because `bandwidth_pll = 30/384000` divides evenly, so
  `int(...)` and `static_cast<unsigned int>(...)` truncate identically. Only the
  constant 15 → 6 matters.
- `m_biquad_phasor_i2` / `_q2` never appear in the old `process()` and are
  absent from the old constructor's initializer list; they were
  default-constructed all-zero and cost 128 bytes in the one `PilotPhaseLock`
  object that exists.

`BiquadIirFilter` and `FirstOrderIirFilter` themselves are unchanged between
the revisions. `sfmbase/Filter.cpp` did change, but only in
`LowPassFilterFirIQ` / `LowPassFilterFirAudio` (the VOLK rewrite of
`doc/LPF_VOLK_20260725.md`), which sit outside the loop.

## 3. The loop on paper

Both revisions run, per I and Q: one all-pole biquad as an in-loop phasor
low-pass, then `atan2`, then a two-tap FIR acting as the proportional term and
stabilizing zero, then the frequency accumulator `m_freq += m_freq_err` and the
phase accumulator — two poles at z = 1.

At F_s = 384000 Hz:

| | `20260716-0` | `dev` |
|---|---|---|
| biquad poles (z) | 0.999438, 0.997386 | 0.999338, 0.996922 |
| biquad corners | 34.4 Hz, 160.0 Hz | **40.5 Hz, 188.4 Hz** |
| biquad DC gain | 1.000509 | 0.999874 |
| first-order zero | z = 0.99994341 (3.459 Hz) | z = 0.99994341 (**3.459 Hz, unchanged**) |
| first-order DC gain | 1.7224×10⁻⁸ | 1.5312×10⁻⁸ |
| natural frequency f_n | 22.6 Hz | 22.3 Hz |
| damping ratio ζ | **0.573** | **0.710** |
| gain crossover | 16.9 Hz | 15.7 Hz |
| phase margin | **46.2°** | **51.6°** |
| gain margin | **19.3 dB** | **21.9 dB** |

The two taps of the first-order section were scaled by exactly the same factor
(new/old = 0.8889688263 for both b0 and b1 to ten significant figures), so the
zero did not move — only the loop gain did. That is the two-knob method of
`doc/PLL_REDESIGN_20260723.md`: widen the biquad, then pull the gain back to
hold f_n. These figures were re-derived independently for this report and agree
with `doc/PLL_ANALYSIS_2_20260723.md` (dev: 51.6° / 21.9 dB) and
`doc/PLL_ANALYSIS_3_20260724.md` (released loop: 46.2° / 19.3 dB).

One point deserves flagging because it is counterintuitive. The dominant-pole
estimate `t_1% ≈ 4.6/(ζω_n)` gives 56 ms (old) and 47 ms (new), but a
sample-by-sample simulation of the *actual* four-pole loop gives **127 ms** and
**131 ms** — the new loop's 1 % settling is marginally *longer*. The extra
low-frequency real closed-loop mode is nearly, but not exactly, cancelled by
the first-order section's zero, and the residual decays slowly. The redesign
bought damping and margin, not raw settling speed.

## 4. Acquisition, as measured

The 6 kHz trace, referred to the steady-state mean of 18999.5332 Hz:

| build | first \|Δf\| < 5 Hz | < 2 Hz | < 1 Hz | peak Δf | at | RMS Δf, 5–200 ms | 200–500 ms |
|---|---|---|---|---|---|---|---|
| `old` | 0.83 ms | 47.0 ms | 72.3 ms | +29.65 Hz | 0.17 ms | 2.155 Hz | 0.961 Hz |
| `new` | 16.50 ms | **37.7 ms** | **53.5 ms** | +29.73 Hz | 0.17 ms | 2.238 Hz | 0.903 Hz |
| `hybloop` | 0.83 ms | 47.0 ms | 72.3 ms | +29.65 Hz | 0.17 ms | 2.155 Hz | 0.961 Hz |
| `hybbq` | 17.17 ms | 35.8 ms | 53.7 ms | +29.74 Hz | 0.17 ms | 2.268 Hz | 0.880 Hz |

`hybloop` reproduces `old` exactly and `hybbq` tracks `new`, so the acquisition
shape is set by the loop filter, and within it mostly by the biquad.

The +29.7 Hz entry is the very first excursion, at t = 0.17 ms, while the
phasor low-pass is still filling and the loop slews across most of its ±30 Hz
range; both builds do it and they do it by the same amount. The interesting
part is what follows. Both loops then overshoot to a plateau at around t = 5 ms
— old to +8.0 Hz, new to +9.9 Hz — and the wider biquad of `dev` lets about
25 % more of that kick through. This is not the f_n = 22 Hz loop mode ringing;
at 5 ms it is the phasor low-pass passing the acquisition transient. By 20 ms
both are within +3.5 Hz and by 80 ms both are inside ±1 Hz.

Two things follow. First, `dev` reaches every band earlier than
`20260716-0` — 53.5 ms versus 72.3 ms to ±1 Hz — despite its slightly larger
initial kick. Second, **neither loop is anywhere near still acquiring at
200 ms**, so the lock-delay reduction is not cutting into the acquisition
transient.

What the loop is doing between 80 ms and 500 ms is tracking a genuinely
disturbed pilot, not settling. Phase-detector RMS over 20–500 ms is 0.217 rad
(old) / 0.180 rad (new) against a steady-state figure of 0.002 rad, and the
residual frequency deviation of ~0.9 Hz RMS in the 200–500 ms window is present
in both builds and in the no-`-E` decode alike. The opening half-second of this
recording simply has a noisy pilot.

## 5. Lock declaration and the stereo onset

| build | expression | samples | time |
|---|---|---|---|
| `20260716-0` | `15.0 / bandwidth` | 192000 | 0.500 s |
| `dev` | `6.0 / bandwidth_pll` | 76800 | 0.200 s |

`m_lock_cnt` accumulates whole blocks of 2048 samples and is tested at block
end, so lock is declared at the end of the first block whose cumulative count
reaches the threshold — block 93 for the old build (t = 0.49600 s), block 37 for
`dev` (t = 0.19733 s). Until then `FmDecoder::process()` takes the
`mono_to_left_right(m_buf_mono, audio)` branch, which copies the mono signal to
both channels, so S = (L−R)/2 is **identically zero**. Measured on the output
files:

| window | old M | old S | new M | new S |
|---|---|---|---|---|
| 0.0000–0.1952 s | −16.50 | **0 exactly** | −16.50 | **0 exactly** |
| 0.1952–0.4939 s | −18.05 | **0 exactly** | −18.05 | **−24.00** |
| 0.4939–0.7000 s | −17.95 | −29.82 | −17.95 | −29.82 |
| 1–2 s | −16.76 | −24.24 | −16.76 | −24.24 |
| 10–20 s | −17.05 | −22.65 | −17.05 | −22.65 |

(dBFS. The onsets measured on the audio, 0.1952 s and 0.4939 s, sit a constant
2.1 ms ahead of the IF-side lock instants — the audio path's own offset — and
their difference, 0.2987 s, is exactly the 56-block difference in the
thresholds.)

That the change is *only* timing is established by construction rather than
argued: `hyblock` (dev with the old threshold) differs from `dev` by

| quantity | `new` vs `hyblock` |
|---|---|
| `m_freq` over all 1,200,000 trace samples | **0.000000 Hz** (exact) |
| `phase_err` over the same | **0.000000 rad** (exact) |
| audio, whole file | −52.25 dBFS |
| audio, discarding the first 0.5 s | **bit-identical** |

and from `old` by −76.24 dBFS whole-file, which is exactly the `dev`-vs-old
figure with the transient removed. The lock delay contributes 100 % of the
difference before 0.5 s and nothing whatever after it.

## 6. Steady state and jitter

Over t ∈ [5, 200] s with the two deep fade windows excluded (192.8 s of data):

| build | mean f | std f (Hz) | phase RMS (rad) | phase peak | mean pilot |
|---|---|---|---|---|---|
| `old` | 18999.5366 | 0.03782 | 0.001983 | 0.04184 | 0.047574 |
| `new` | 18999.5366 | **0.03572** | 0.002105 | 0.04371 | 0.047544 |
| `hybloop` | 18999.5366 | 0.03782 | 0.001983 | 0.04184 | 0.047574 |
| `hybbq` | 18999.5366 | 0.03430 | 0.002014 | 0.04361 | 0.047574 |
| `hybatan` | 18999.5366 | 0.03572 | 0.002105 | 0.04371 | 0.047544 |
| `hyblock` | 18999.5366 | 0.03572 | 0.002105 | 0.04371 | 0.047544 |

Trajectory differences against `new` over the same window:

| build | Δf RMS (Hz) | Δf max | Δphase RMS (rad) | Δphase max |
|---|---|---|---|---|
| `old` | 5.37×10⁻³ | 0.117 | 3.04×10⁻⁴ | 6.6×10⁻³ |
| `hybloop` | 5.37×10⁻³ | 0.117 | 3.04×10⁻⁴ | 6.6×10⁻³ |
| `hybbq` | 5.75×10⁻³ | 0.150 | 3.46×10⁻⁴ | 9.2×10⁻³ |
| `hybatan` | 1.12×10⁻⁷ | 2.4×10⁻⁶ | 6.4×10⁻⁹ | 1.4×10⁻⁷ |
| `hyblock` | **0** | **0** | **0** | **0** |

`hybloop` matches `old` to every digit shown, which closes the attribution: the
loop filter coefficients account for the entire steady-state difference, with
nothing left for the phase detector or the lock delay.

`hybbq` — old biquad with the *new* gains — deviates from `dev` slightly *more*
than the fully reverted `hybloop` does (5.75 vs 5.37 mHz RMS, 0.150 vs 0.117 Hz
peak). Pairing a narrow low-pass with gains that were rescaled to compensate
for a wide one is a mismatched combination. It is direct evidence that the two
coefficient changes are one design, not two.

The closed-loop noise shaping, as a Welch PSD ratio of `m_freq` between the
builds (which cancels the common input phase noise):

| band | `new` / `old` | `hybbq` / `old` | `old` RMS in band |
|---|---|---|---|
| 0.1–1 Hz | +0.02 dB | +0.02 dB | 0.0433 Hz |
| 1–3 Hz | +0.13 dB | +0.13 dB | 0.0722 Hz |
| 3–10 Hz | +0.24 dB | +0.36 dB | 0.1552 Hz |
| 10–20 Hz | **−0.51 dB** | −0.06 dB | 0.1974 Hz |
| 20–30 Hz | **−1.07 dB** | −0.85 dB | 0.1485 Hz |
| 30–50 Hz | −0.60 dB | −1.02 dB | 0.1119 Hz |
| 50–100 Hz | +0.04 dB | −1.04 dB | 0.0611 Hz |
| 100–300 Hz | +0.55 dB | −1.65 dB | 0.0222 Hz |
| 300–1000 Hz | **+1.19 dB** | −1.38 dB | 0.0024 Hz |

This is the retune's signature, and it matches the design intent. The `dev`
loop passes about 1 dB less noise across its resonance region (10–50 Hz) —
ζ = 0.573 predicts closed-loop peaking at f_n·√(1−2ζ²) ≈ 13 Hz, while ζ = 0.710
is essentially at the maximally flat point and should not peak at all — and
about 1 dB more above 100 Hz, which is the wider phasor low-pass. The
smoothed PSD maximum moves from 12.6 Hz (`old`) to 9.2 Hz (`dev`).

Everything here is a fraction of a dB on a quantity that is already 114 dB
below the carrier. The retune is not audible in steady state; it is audible, if
at all, only in the transient response — which is §7.

## 7. The three pilot fades

The decode contains three brief, deep drops in pilot amplitude. Two are fades
in the recording; the third, as §7.1 shows, is self-inflicted. The dense trace
resolves all three; the status line resolves none of them.

| event | pilot minimum | depth | below −2.5 dB for |
|---|---|---|---|
| t = 0.5370 s | 0.030237 | −4.02 dB | 5.0 ms |
| t = 52.1485 s | 0.024117 | **−5.98 dB** | 5.5 ms |
| t = 130.3803 s | 0.023933 | **−6.05 dB** | 6.2 ms |

(against a run-median `m_pilot_level` of 0.048006; `minsignal/2` is 0.0005, so
the loop was never remotely close to declaring loss of lock, and indeed
`m_lock_cnt` never fell once in either build for the whole run.)

How each loop responds:

| event | build | peak Δf | at | peak phase err | back inside ±1 Hz | inside ±0.2 Hz |
|---|---|---|---|---|---|---|
| 0.537 s | `old` | +24.28 Hz | +5.8 ms | 1.178 rad | 50.3 ms | 91.8 ms |
| | `new` | +22.36 Hz | +4.8 ms | 1.235 rad | 53.3 ms | 92.0 ms |
| 52.148 s | `old` | **−29.53 Hz** | +3.2 ms | −1.515 rad | 47.8 ms | 97.0 ms |
| | `new` | −28.52 Hz | +4.0 ms | −1.585 rad | 53.7 ms | 106.7 ms |
| 130.380 s | `old` | **−29.53 Hz** | +3.3 ms | −1.503 rad | 48.3 ms | 115.0 ms |
| | `new` | −28.40 Hz | +4.2 ms | −1.574 rad | 53.7 ms | 119.3 ms |

The trade is visible and small: the old loop returns to a given band about
6 ms sooner, the new loop swings about 1 Hz less far and peaks slightly later.
On its own that would be a wash.

What is not a wash is the −29.53 Hz figure. `PilotPhaseLock` clamps `m_freq`
to ±`bandwidth_pll` around nominal, i.e. to [18970, 19030] Hz. The steady-state
pilot sits at 18999.5332 Hz, so −29.53 Hz **is** the rail. The old loop pins
`m_freq` at exactly 18970.000000 Hz:

| build | min `m_freq` over the run | trace samples at the rail |
|---|---|---|
| `old` | **18970.000000** | 19 — t = 52.15167…52.15300 s (≥1.33 ms), t = 130.38367…130.38517 s (≥1.50 ms) |
| `hybloop` | **18970.000000** | 19, same instants |
| `new` | 18971.010377 | none |
| `hybbq` | 18971.457608 | none |
| `hybatan` | 18971.010376 | none |
| `hyblock` | 18971.010377 | none |

While the limiter is active the loop is open — the integrator's demand is being
discarded — so this is the one condition in the whole run where the two loops
are not merely differently tuned but qualitatively different. The retuned loop
keeps about 1 Hz of headroom against a limit the old loop hits. Both hybrids
show the responsible knob is the PI gain: `hybbq` keeps the old (narrow)
biquad but the new, smaller gains, and it does not saturate.

The audio consequence, measured as `new` − `hybloop` so that the lock delay and
the phase detector are held constant and only the retune varies:

| window | RMS | peak 10 ms frame | share of the retune difference energy |
|---|---|---|---|
| 0.50–0.70 s | −56.91 dBFS | −47.77 dBFS at 0.55 s | 8.54 % |
| 51.90–52.50 s | −51.57 dBFS | **−38.96 dBFS at 52.18 s** | **87.60 %** |
| 130.10–130.70 s | −65.32 dBFS | −52.45 dBFS at 130.40 s | 3.70 % |
| everything else, t > 0.5 s | **−124.28 dBFS** | — | 0.00 % |

Whole file, `new` vs `hybloop`: −76.23 dBFS. So **99.84 % of everything the
retune does to this recording happens inside three windows totalling 1.4
seconds**, each one aligned with a pilot fade, and the largest is 68.9 ms wide
at −37.25 dBFS peak envelope.

### 7.1 The 0.537 s event is the multipath filter, not the channel

The first of the three is not a channel fade. `FmDecoder` bypasses the
multipath filter for its first 100 blocks (`m_wait_multipath_blocks(100)`,
`sfmbase/FmDecode.cpp:35`), which at 2048 samples per block is exactly 204,800
IF samples = **0.533333 s**. Decoding the same file with `-E 36` and with no
`-E` at all gives PLL traces that are equal sample for sample up to trace index
204,800 and diverge from that sample onward:

| t | Δf with `-E 36` | Δf without `-E` | pilot with `-E 36` | pilot without `-E` |
|---|---|---|---|---|
| 0.530 s | −0.13 Hz | −0.13 Hz | 0.045066 | 0.045066 |
| 0.540 s | **+21.65 Hz** | +0.13 Hz | 0.035458 | 0.044627 |
| 0.550 s | +13.69 Hz | +0.89 Hz | 0.040939 | 0.044731 |
| 0.570 s | −2.24 Hz | −0.08 Hz | 0.045574 | 0.045935 |
| peak over 0.50–0.70 s | **22.36 Hz** | 1.51 Hz | | |

When the filter engages its coefficients are still the initial ones — reference
tap `1+0j`, everything else zero — so the signal path acquires the reference
tap's group delay of `stages*3+1` = 109 samples in one block boundary. At
384 kHz that is 284 µs, or 5.4 cycles of the 19 kHz pilot, applied as a step.
The loop sees a phase step and takes ~90 ms to absorb it.

This is present in both builds and is not part of the old-versus-new
difference. It is worth recording anyway: it is the largest single disturbance
the PLL sees in the first second apart from acquisition itself, it is entirely
self-inflicted, and it lands 37 ms after the old build declares stereo lock.

### 7.2 What the multipath filter is worth at the pilot

The same pair of decodes gives the filter's effect on PLL input quality, over
t ∈ [5, 200] s with the two deep fades excluded:

| | `m_freq` std | phase error RMS | mean pilot level |
|---|---|---|---|
| with `-E 36` | **0.0357 Hz** | **0.002105 rad** | 0.047544 |
| without `-E` | 0.8349 Hz | 0.048589 rad | 0.044635 |

The multipath filter improves the pilot's phase stability at the loop by
**27 dB** on this recording (27.3 dB in phase error, 27.4 dB in the frequency
estimate). That is a much larger number than anything else
in this report, and it is worth holding next to `MF_DIFFERENCE_20260727.md` §7:
the filter's own effect dwarfs every build-to-build difference discussed here.

## 8. The phase detector

`hybatan` is `dev` with the GNU Radio table-based `fast_atan2f` restored. The
table has 257 entries with linear interpolation and an exact small-angle branch;
its error against `atan2` is 1.54×10⁻⁶ rad worst case over the full range and
6.9×10⁻⁸ rad RMS over the ±0.02 rad range the loop occupies when locked, with a
local derivative of exactly 1.0 at zero — so no detector gain error.

Regressing `phase_err` of `hybatan` on `phase_err` of `dev` over the steady
state:

| quantity | value |
|---|---|
| slope | 1.000000 |
| intercept | −1.3×10⁻¹² rad |
| residual RMS | 6.4×10⁻⁹ rad |
| `m_freq` difference, RMS / max | 1.1×10⁻⁷ / 2.4×10⁻⁶ Hz |
| audio difference, whole file | **−171.04 dBFS** |

No offset, no gain error, no added noise above double-precision rounding. The
swap is a code-hygiene change — it removed a GPL-attributed third-party table —
and it neither helped nor hurt the loop.

## 9. Attribution of the audio difference

Putting §5, §7 and §8 together, the −52.24 dBFS whole-file difference between
`20260716-0` and `dev` decomposes as:

| contribution | measured as | level | where it lives |
|---|---|---|---|
| lock delay 0.500 → 0.200 s | `new` vs `hyblock` | −52.25 dBFS | t ∈ [0.195, 0.494] s only |
| loop retune (biquad + PI) | `new` vs `hybloop` | −76.23 dBFS | 99.84 % inside three fade windows |
| phase detector | `new` vs `hybatan` | −171.04 dBFS | uniformly nothing |
| everything outside the PLL | `old` vs `hybloop`, after 0.5 s | −149.99 dBFS | the FIR VOLK rewrite, M-channel only |

The four rows are independent: `hybloop` reproduces `old` after 0.5 s to
−149.99 dBFS, and `hyblock` reproduces `dev` after 0.5 s bit-for-bit.

## 10. What this establishes, and what it does not

- **It establishes that the 500 → 200 ms lock-delay reduction is safe on this
  recording.** The loop is inside ±1 Hz at 53.5 ms and inside ±0.2 Hz well
  before 200 ms; the threshold retains about 4× margin. It does not establish
  that 200 ms is safe on a weak or fading signal where acquisition is slower,
  and the trace method here is exactly what should be re-run on such a
  recording before that claim is made.
- **It establishes that the retune's only measurable consequence on this
  recording is transient.** Steady-state trajectories agree to 5.4 mHz, the
  jitter spectra agree within 1.1 dB, and 99.84 % of the audio difference is
  three windows totalling 1.4 s.
- **It establishes one concrete robustness improvement**: the old loop
  saturates its ±30 Hz frequency limiter at both deep fades and the retuned loop
  does not. Whether the 6 ms slower recovery of the retuned loop outweighs
  1.3–1.5 ms of open-loop operation twice in 200 seconds is a listening
  question, not a measurement question.
- **It corrects `MF_DIFFERENCE_20260727.md` §6.** The bursts at 52.18 s and
  130.40 s *do* coincide with pilot fades — −5.98 dB and −6.05 dB, 5–6 ms wide.
  The earlier conclusion came from the status line, which reports a 10-block
  moving average of `2 * m_pilot_level` printed every 20 blocks; a 5 ms fade is
  invisible to it. The earlier report also missed the 0.537 s event entirely,
  because it falls inside the 0–1 s bucket that the startup transient
  dominates.
- **It does not evaluate audio quality.** Nothing here says which loop sounds
  better. The measured differences are a −38.96 dBFS peak frame twice in 200
  seconds and a fraction of a dB of jitter shaping.

## 11. Reproduction

```sh
S=/tmp/pll                       # scratch
for v in old new hybloop hybbq hybatan hyblock; do
  case $v in old) ref=20260716-0 ;; *) ref=dev ;; esac
  git worktree add --detach $S/$v $ref
  cp -R r8brain-free-src $S/$v/ && rm -f $S/$v/r8brain-free-src/.git
done
# hybrids: edit sfmbase/PilotPhaseLock.cpp in the corresponding worktree
#   hybloop  biquad -> (1.46974784e-06,0,0,-1.99682419,0.996825659)  (both i1,q1)
#            m_first_phase_err -> (0.000304341788,-0.000304324564,0)
#   hybbq    biquad only
#   hyblock  m_lock_delay -> static_cast<unsigned int>(15.0 / bandwidth_pll)
#   hybatan  git checkout 20260716-0 -- include/Utility.h; then
#            std::atan2(...) -> Utility::fast_atan2f(...) and include "Utility.h"
for v in old new hybloop hybbq hybatan hyblock; do
  cmake -S $S/$v -B $S/$v/build -DEXTRA_FLAGS="-DDEBUG_PLL_FILTER -DDEBUG_PLL_TRACE"
  cmake --build $S/$v/build --target airspy-fmradion
done
```

`DEBUG_PLL_TRACE` is the flag added for this measurement. Insert it in
`PilotPhaseLock::process()` immediately after the `m_freq` clamp, next to the
existing `DEBUG_PLL_FILTER` block:

```cpp
#ifdef DEBUG_PLL_TRACE
    {
      std::uint64_t idx = m_sample_cnt + i;
      if ((idx & 63ULL) == 0) {
        fmt::println(stderr, "T,{},{:.12g},{:.9g},{:.9g},{:.9g},{}", idx,
                     m_freq * sample_rate_if / (2.0 * M_PI),
                     m_freq_err * sample_rate_if / (2.0 * M_PI), phase_err,
                     m_pilot_level, m_lock_cnt);
      }
    }
#endif
```

Run the decode of §1 with each binary, redirecting stderr to a log, then split
the glued status lines before parsing:

```sh
perl -pe 's/(:Pilot=\s*-?[\d.]+)(T,)/$1\n$2/' < raw.log > trace.log
grep -c '^T,' trace.log     # must be exactly 1200000 for a 200 s file
```

The three numbers that settle the attribution:

```python
import numpy as np, soundfile as sf
db = lambda v: 20 * np.log10(np.sqrt(np.mean(v ** 2)))
a, sr = sf.read("new.wav", dtype="float64", always_2d=True)
for other in ("hyblock", "hybloop", "hybatan"):
    b, _ = sf.read(f"{other}.wav", dtype="float64", always_2d=True)
    n = min(len(a), len(b))
    i = int(0.5 * sr)
    print(other, f"{db(a[:n] - b[:n]):.2f} dBFS whole file,",
          f"{db(a[i:n] - b[i:n]):.2f} dBFS after 0.5 s")
```

`hyblock` must print `-inf` for the second figure — that is the proof that the
lock delay is pure bookkeeping. `hybloop` must print about −76.2 dBFS, and
`hybatan` about −171 dBFS.
